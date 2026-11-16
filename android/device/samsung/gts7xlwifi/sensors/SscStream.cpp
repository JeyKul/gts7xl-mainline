#include "SscStream.h"

#include <log/log.h>
#include <string.h>

extern "C" {
#include "client.h"
#include "ssc.h"
}

namespace {

struct Ctx {
    SscStream* self;
    ssc_uid uid;
};

void onEvent(void* ctx, ssc_uid uid, uint32_t id, uint64_t, const uint8_t* b, size_t l) {
    auto* c = static_cast<Ctx*>(ctx);
    if (uid.low != c->uid.low || uid.high != c->uid.high || id != SSC_MSG_REPORT) return;
    float v[8];
    int acc = 0;
    int n = ssc_parse_sample(b, l, v, 8, &acc);
    if (n > 0) c->self->onSample(v, n, acc);
}

struct Attrs {
    int onChange = 0;
    float minRate = 0;
};

void onAttr(void* ctx, int id, const ssc_attr_value* v) {
    auto* a = static_cast<Attrs*>(ctx);
    if (id == SSC_ATTR_STREAM_TYPE && v->kind == ssc_attr_value::ATTR_INT)
        a->onChange = v->i == 1;
    else if (id == SSC_ATTR_RATES && v->kind == ssc_attr_value::ATTR_FLOAT && (!a->minRate || v->f < a->minRate))
        a->minRate = v->f;
}

}  // namespace

// Read a sensor's attributes (stream type, rates): same request as sensord's
static int getAttrs(client* c, ssc_uid uid, Attrs* a) {
    uint8_t body[16];
    size_t n = ssc_body_attr(body, sizeof(body));
    int ret = client_request(c, uid, SSC_MSG_GET_ATTRIBUTES, body, n, true);
    if (ret) return ret;
    struct Wait {
        Attrs* a;
        ssc_uid uid;
        int done;
    } w{a, uid, 0};
    return client_pump(
            c,
            [](void* ctx, ssc_uid u, uint32_t id, uint64_t, const uint8_t* b, size_t l) {
                auto* w = static_cast<Wait*>(ctx);
                if (u.low != w->uid.low || u.high != w->uid.high || id != SSC_MSG_ATTRIBUTES) return;
                ssc_parse_attrs(b, l, onAttr, w->a);
                w->done = 1;
            },
            &w, &w.done);
}

SscStream::SscStream(std::string type, float rateHz)
    : mType(std::move(type)), mRate(rateHz), mThread([this] { run(); }) {}

SscStream::~SscStream() {
    {
        std::lock_guard l(mLock);
        mExit = true;
        mStop = 1;
        mCv.notify_all();
    }
    mThread.join();
}

void SscStream::acquire() {
    std::lock_guard l(mLock);
    if (mUsers++ == 0) {
        mStop = 0;
        mCv.notify_all();
    }
}

void SscStream::release() {
    std::lock_guard l(mLock);
    if (mUsers > 0 && --mUsers == 0) {
        mStop = 1;
        mValid = false;
    }
}

bool SscStream::latest(std::array<float, 3>* v, int* accuracy) {
    std::lock_guard l(mLock);
    if (!mValid) return false;
    *v = mValue;
    *accuracy = mAccuracy;
    return true;
}

void SscStream::onSample(const float* v, int n, int accuracy) {
    std::lock_guard l(mLock);
    mValue = {v[0], n > 1 ? v[1] : 0.f, n > 2 ? v[2] : 0.f};
    mAccuracy = accuracy;
    mValid = true;
}

void SscStream::run() {
    std::unique_lock l(mLock);
    while (!mExit) {
        mCv.wait(l, [this] { return mExit || mUsers > 0; });
        if (mExit) break;
        l.unlock();
        int ret = streamOnce();
        l.lock();
        // The DSP may still be booting, or restarted: try again while wanted
        if (ret && mUsers > 0 && !mExit) mCv.wait_for(l, std::chrono::seconds(2));
    }
}

// Several streams starting at once confuse the SUID lookups: one at a time
static std::mutex sSetupLock;

// One streaming session; returns when released (0) or on failure
int SscStream::streamOnce() {
    std::unique_lock setup(sSetupLock);
    client c;
    int ret = client_open(&c, 5);
    if (ret) {
        ALOGW("%s: SSC service not found (%d)", mType.c_str(), ret);
        return ret;
    }
    // NO_WAKEUP (value guessed in sensord) made the DSP hold back gyro, mag
    // and light samples; wakeup delivery streams them
    c.delivery = SSC_DELIVERY_WAKEUP;
    ssc_uid uids[8];
    Ctx ctx{this, {}};
    Attrs a;
    ret = client_suid(&c, mType.c_str(), true, uids, 8);
    if (ret <= 0) {
        ALOGW("%s: no such sensor (%d)", mType.c_str(), ret);
        client_close(&c);
        return ret ? ret : -ENODEV;
    }
    ctx.uid = uids[0];
    c.timeout_ms = 3000;
    if (getAttrs(&c, ctx.uid, &a)) ALOGW("%s: no attributes, assuming continuous", mType.c_str());
    if (a.onChange) {
        ret = client_request(&c, ctx.uid, SSC_MSG_ENABLE_ON_CHANGE, nullptr, 0, false);
    } else {
        uint8_t body[32];
        float rate = a.minRate && mRate < a.minRate ? a.minRate : mRate;
        size_t n = ssc_body_rate(body, sizeof(body), rate);
        ret = client_request(&c, ctx.uid, SSC_MSG_ENABLE_CONTINUOUS, body, n, true);
    }
    if (ret) {
        ALOGW("%s: enable failed (%d)", mType.c_str(), ret);
        client_close(&c);
        return ret;
    }
    ALOGI("%s: streaming", mType.c_str());
    setup.unlock();
    // On-change sensors can stay quiet for long; mStop ends the pump
    c.timeout_ms = a.onChange ? 1000 : 10000;
    do {
        ret = client_pump(&c, onEvent, &ctx, &mStop);
    } while (ret == -ETIMEDOUT && a.onChange && !mStop);
    c.timeout_ms = 1000;
    client_request(&c, ctx.uid, SSC_MSG_DISABLE, nullptr, 0, false);
    client_close(&c);
    return mStop ? 0 : ret;
}
