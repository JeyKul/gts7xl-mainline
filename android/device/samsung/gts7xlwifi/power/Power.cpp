/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Power.h"
#include "PowerHintSession.h"

#include <android-base/logging.h>
#include <fmq/AidlMessageQueue.h>
#include <fmq/EventFlag.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace aidl {
namespace android {
namespace hardware {
namespace power {
namespace impl {
namespace example {

using namespace std::chrono_literals;
using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::aidl::android::hardware::power::ChannelMessage;
using ::aidl::android::hardware::power::CompositionData;
using ::aidl::android::hardware::power::CompositionUpdate;
using ::android::AidlMessageQueue;

using ndk::ScopedAStatus;

const std::vector<Boost> BOOST_RANGE{ndk::enum_range<Boost>().begin(),
                                     ndk::enum_range<Boost>().end()};
const std::vector<Mode> MODE_RANGE{ndk::enum_range<Mode>().begin(), ndk::enum_range<Mode>().end()};

template <class T>
constexpr size_t enum_size() {
    return static_cast<size_t>(*(ndk::enum_range<T>().end() - 1)) + 1;
}

// gts7xl: the UI's CPU clock floor. schedutil ramps up too slowly for smooth
// frames; uclamp.min of the top-app and foreground cpu cgroups (the app and
// SurfaceFlinger) lifts their frequency while the user interacts or an app
// launches, and stays at a low baseline otherwise.
class Booster {
  public:
    Booster() : mBase(::android::base::GetIntProperty("vendor.power.uclamp_base", 30)) {
        readGpu();
        apply(mBase);
        std::thread([this] { run(); }).detach();
    }
    void boostFor(std::chrono::milliseconds d) {
        std::lock_guard l(mLock);
        mUntil = std::max(mUntil, std::chrono::steady_clock::now() + d);
        mCv.notify_one();
    }
    void hold(Mode mode, bool on) {
        std::lock_guard l(mLock);
        uint64_t bit = 1ull << static_cast<int>(mode);
        mHolds = on ? (mHolds | bit) : (mHolds & ~bit);
        mCv.notify_one();
    }
    void setInteractive(bool on) {
        std::lock_guard l(mLock);
        mInteractive = on;
        mCv.notify_one();
    }

  private:
    static constexpr const char* kGpu = "/sys/class/devfreq/3d00000.gpu/";

    void apply(int value) {
        for (const char* g : {"top-app", "foreground"}) {
            std::string f = std::string("/dev/cpuctl/") + g + "/cpu.uclamp.min";
            if (!::android::base::WriteStringToFile(std::to_string(value), f))
                PLOG(ERROR) << "write " << f;
        }
        // GPU: boosted, its lowest clock is its highest; otherwise devfreq's own floor
        if (!mGpuMin.empty())
            ::android::base::WriteStringToFile(value == 100 ? mGpuMax : mGpuMin,
                                               std::string(kGpu) + "min_freq");
    }

    void readGpu() {
        std::string avail;
        if (!::android::base::ReadFileToString(std::string(kGpu) + "available_frequencies", &avail))
            return;
        auto f = ::android::base::Split(::android::base::Trim(avail), " ");
        if (f.empty()) return;
        long lo = -1, hi = -1;
        for (auto& x : f) {
            long v = std::atol(x.c_str());
            if (v <= 0) continue;
            if (lo < 0 || v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (lo > 0) {
            mGpuMin = std::to_string(lo);
            mGpuMax = std::to_string(hi);
        }
    }

    std::string mGpuMin, mGpuMax;
    void run() {
        std::unique_lock l(mLock);
        int current = -1;
        for (;;) {
            auto now = std::chrono::steady_clock::now();
            bool boosted = mHolds != 0 || now < mUntil;
            int want = boosted ? 100 : (mInteractive ? mBase : 0);
            if (want != current) {
                apply(want);
                current = want;
            }
            if (mHolds == 0 && now < mUntil)
                mCv.wait_until(l, mUntil);
            else
                mCv.wait(l);
        }
    }

    const int mBase;
    std::mutex mLock;
    std::condition_variable mCv;
    std::chrono::steady_clock::time_point mUntil;
    uint64_t mHolds = 0;
    bool mInteractive = true;
};

static Booster& booster() {
    static Booster* b = new Booster();
    return *b;
}

ScopedAStatus Power::setMode(Mode type, bool enabled) {
    LOG(VERBOSE) << "Power setMode: " << static_cast<int32_t>(type) << " to: " << enabled;
    switch (type) {
        case Mode::INTERACTIVE:
            booster().setInteractive(enabled);
            break;
        case Mode::LAUNCH:
        case Mode::EXPENSIVE_RENDERING:
        case Mode::GAME_LOADING:
            booster().hold(type, enabled);
            break;
        default:
            break;
    }
    return ScopedAStatus::ok();
}

ScopedAStatus Power::isModeSupported(Mode type, bool* _aidl_return) {
    LOG(INFO) << "Power isModeSupported: " << static_cast<int32_t>(type);
    *_aidl_return = type >= MODE_RANGE.front() && type <= MODE_RANGE.back();
    return ScopedAStatus::ok();
}

ScopedAStatus Power::setBoost(Boost type, int32_t durationMs) {
    LOG(VERBOSE) << "Power setBoost: " << static_cast<int32_t>(type)
                 << ", duration: " << durationMs;
    using std::chrono::milliseconds;
    switch (type) {
        case Boost::INTERACTION:
            // Touch: keep the floor through the scroll/fling that follows
            booster().boostFor(milliseconds(std::max(durationMs, 1500)));
            break;
        case Boost::DISPLAY_UPDATE_IMMINENT:
            booster().boostFor(milliseconds(100));
            break;
        default:
            break;
    }
    return ScopedAStatus::ok();
}

ScopedAStatus Power::isBoostSupported(Boost type, bool* _aidl_return) {
    LOG(INFO) << "Power isBoostSupported: " << static_cast<int32_t>(type);
    *_aidl_return = type >= BOOST_RANGE.front() && type <= BOOST_RANGE.back();
    return ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getCpuHeadroom(const CpuHeadroomParams&, CpuHeadroomResult*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::getGpuHeadroom(const GpuHeadroomParams&, GpuHeadroomResult*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ScopedAStatus Power::createHintSession(int32_t, int32_t, const std::vector<int32_t>& tids, int64_t,
                                       std::shared_ptr<IPowerHintSession>* _aidl_return) {
    if (tids.size() == 0) {
        *_aidl_return = nullptr;
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::shared_ptr<IPowerHintSession> powerHintSession =
            ndk::SharedRefBase::make<PowerHintSession>();
    mPowerHintSessions.push_back(powerHintSession);
    *_aidl_return = powerHintSession;
    return ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::createHintSessionWithConfig(
        int32_t tgid, int32_t uid, const std::vector<int32_t>& threadIds, int64_t durationNanos,
        SessionTag, SessionConfig* config, std::shared_ptr<IPowerHintSession>* _aidl_return) {
    auto out = createHintSession(tgid, uid, threadIds, durationNanos, _aidl_return);
    static_cast<PowerHintSession*>(_aidl_return->get())->getSessionConfig(config);
    return out;
}

ndk::ScopedAStatus Power::getSessionChannel(int32_t, int32_t, ChannelConfig* _aidl_return) {
    static AidlMessageQueue<ChannelMessage, SynchronizedReadWrite> stubQueue{20, true};
    static std::thread stubThread([&] {
        ChannelMessage data;
        // This loop will only run while there is data waiting
        // to be processed, and blocks on a futex all other times
        while (stubQueue.readBlocking(&data, 1, 0)) {
        }
    });
    _aidl_return->channelDescriptor = stubQueue.dupeDesc();
    _aidl_return->readFlagBitmask = 0x01;
    _aidl_return->writeFlagBitmask = 0x02;
    _aidl_return->eventFlagDescriptor = std::nullopt;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::closeSessionChannel(int32_t, int32_t) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getHintSessionPreferredRate(int64_t* outNanoseconds) {
    *outNanoseconds = std::chrono::nanoseconds(1ms).count();
    return ScopedAStatus::ok();
}

template <class E>
int64_t bitsForEnum() {
    return static_cast<int64_t>(std::bitset<enum_size<E>()>().set().to_ullong());
}

ndk::ScopedAStatus Power::getSupportInfo(SupportInfo* _aidl_return) {
    static SupportInfo supportInfo = {.usesSessions = false,
                                      .modes = bitsForEnum<Mode>(),
                                      .boosts = bitsForEnum<Boost>(),
                                      .sessionHints = 0,
                                      .sessionModes = 0,
                                      .sessionTags = 0,
                                      .compositionData = {
                                              .isSupported = false,
                                              .disableGpuFences = false,
                                              .maxBatchSize = 1,
                                              .alwaysBatch = false,
                                      },
                                      .headroom = {
                                              .isCpuSupported = false,
                                              .isGpuSupported = false,
                                              .cpuMinIntervalMillis = 0,
                                              .gpuMinIntervalMillis = 0,
                                      }};
    // Copy the support object into the binder
    *_aidl_return = supportInfo;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::sendCompositionData(const std::vector<CompositionData>&) {
    LOG(INFO) << "Composition data received!";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::sendCompositionUpdate(const CompositionUpdate&) {
    LOG(INFO) << "Composition update received!";
    return ndk::ScopedAStatus::ok();
}

}  // namespace example
}  // namespace impl
}  // namespace power
}  // namespace hardware
}  // namespace android
}  // namespace aidl
