/*
 * Vibrator HAL for the Galaxy Tab S7+ on mainline Linux.
 *
 * The motor is a plain ERM on a fixed-voltage regulator, exposed by the
 * kernel's regulator-haptic driver as an input device with FF_RUMBLE.
 * There is no amplitude control (the regulator has one fixed voltage), so
 * effects are just on-times, kept short: the motor is strong at full voltage
 * and slider ticks (TEXTURE_TICK) come in bursts.
 */

#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aidl::android::hardware::vibrator {

static constexpr const char* kInputName = "regulator-haptic";

static int openHaptic() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return -1;
    int fd = -1;
    while (dirent* de = readdir(dir)) {
        if (strncmp(de->d_name, "event", 5)) continue;
        std::string path = std::string("/dev/input/") + de->d_name;
        int f = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (f < 0) continue;
        char name[64] = {};
        if (ioctl(f, EVIOCGNAME(sizeof(name) - 1), name) >= 0 && !strcmp(name, kInputName)) {
            fd = f;
            break;
        }
        close(f);
    }
    closedir(dir);
    return fd;
}

class Vibrator : public BnVibrator {
  public:
    Vibrator() : mFd(openHaptic()) {
        if (mFd < 0) LOG(ERROR) << "no " << kInputName << " input device";
    }

    ndk::ScopedAStatus getCapabilities(int32_t* ret) override {
        *ret = IVibrator::CAP_ON_CALLBACK | IVibrator::CAP_PERFORM_CALLBACK;
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus off() override {
        std::lock_guard lock(mMutex);
        mGeneration++;
        play(0);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus on(int32_t timeoutMs,
                          const std::shared_ptr<IVibratorCallback>& cb) override {
        if (timeoutMs <= 0) return off();
        return start(timeoutMs, cb);
    }

    ndk::ScopedAStatus perform(Effect effect, EffectStrength strength,
                               const std::shared_ptr<IVibratorCallback>& cb,
                               int32_t* ret) override {
        int ms;
        switch (effect) {
            case Effect::TEXTURE_TICK: ms = 4; break;
            case Effect::TICK: ms = 7; break;
            case Effect::CLICK: ms = 12; break;
            case Effect::HEAVY_CLICK: ms = 20; break;
            case Effect::DOUBLE_CLICK: ms = 12; break;
            default: return unsupported();
        }
        switch (strength) {
            case EffectStrength::LIGHT: ms = ms * 3 / 4; break;
            case EffectStrength::MEDIUM: break;
            case EffectStrength::STRONG: ms = ms * 5 / 4; break;
            default: return unsupported();
        }
        if (effect == Effect::DOUBLE_CLICK) {
            // two pulses with a gap the motor can stop in
            *ret = ms + kDoubleGapMs + ms;
            return startDouble(ms, cb);
        }
        *ret = ms;
        return start(ms, cb);
    }

    ndk::ScopedAStatus getSupportedEffects(std::vector<Effect>* ret) override {
        *ret = {Effect::TICK, Effect::TEXTURE_TICK, Effect::CLICK, Effect::HEAVY_CLICK,
                Effect::DOUBLE_CLICK};
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus performVendorEffect(const VendorEffect&,
                                           const std::shared_ptr<IVibratorCallback>&) override {
        return unsupported();
    }
    ndk::ScopedAStatus setAmplitude(float) override { return unsupported(); }
    ndk::ScopedAStatus setExternalControl(bool) override { return unsupported(); }
    ndk::ScopedAStatus getCompositionDelayMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getCompositionSizeMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getSupportedPrimitives(std::vector<CompositePrimitive>* ret) override {
        ret->clear();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getPrimitiveDuration(CompositePrimitive, int32_t*) override {
        return unsupported();
    }
    ndk::ScopedAStatus compose(const std::vector<CompositeEffect>&,
                               const std::shared_ptr<IVibratorCallback>&) override {
        return unsupported();
    }
    ndk::ScopedAStatus getSupportedAlwaysOnEffects(std::vector<Effect>* ret) override {
        ret->clear();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus alwaysOnEnable(int32_t, Effect, EffectStrength) override {
        return unsupported();
    }
    ndk::ScopedAStatus alwaysOnDisable(int32_t) override { return unsupported(); }
    ndk::ScopedAStatus getResonantFrequency(float*) override { return unsupported(); }
    ndk::ScopedAStatus getQFactor(float*) override { return unsupported(); }
    ndk::ScopedAStatus getFrequencyResolution(float*) override { return unsupported(); }
    ndk::ScopedAStatus getFrequencyMinimum(float*) override { return unsupported(); }
    ndk::ScopedAStatus getBandwidthAmplitudeMap(std::vector<float>*) override {
        return unsupported();
    }
    ndk::ScopedAStatus getPwlePrimitiveDurationMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getPwleCompositionSizeMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus getSupportedBraking(std::vector<Braking>*) override {
        return unsupported();
    }
    ndk::ScopedAStatus composePwle(const std::vector<PrimitivePwle>&,
                                   const std::shared_ptr<IVibratorCallback>&) override {
        return unsupported();
    }
    ndk::ScopedAStatus getFrequencyToOutputAccelerationMap(
            std::vector<FrequencyAccelerationMapEntry>*) override {
        return unsupported();
    }
    ndk::ScopedAStatus getPwleV2PrimitiveDurationMaxMillis(int32_t*) override {
        return unsupported();
    }
    ndk::ScopedAStatus getPwleV2PrimitiveDurationMinMillis(int32_t*) override {
        return unsupported();
    }
    ndk::ScopedAStatus getPwleV2CompositionSizeMax(int32_t*) override { return unsupported(); }
    ndk::ScopedAStatus composePwleV2(const CompositePwleV2&,
                                     const std::shared_ptr<IVibratorCallback>&) override {
        return unsupported();
    }

  private:
    static ndk::ScopedAStatus unsupported() {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    // Caller holds mMutex. ff-memless stops the effect after replay.length.
    bool play(int ms) {
        if (mFd < 0) return false;
        input_event ev = {};
        ev.type = EV_FF;
        if (ms > 0) {
            ff_effect effect = {};
            effect.type = FF_RUMBLE;
            effect.id = mEffectId;
            effect.replay.length = ms;
            effect.u.rumble.strong_magnitude = 0xffff;
            if (ioctl(mFd, EVIOCSFF, &effect) < 0) {
                PLOG(ERROR) << "EVIOCSFF";
                return false;
            }
            mEffectId = effect.id;
            ev.value = 1;
        }
        if (mEffectId < 0) return true;
        ev.code = mEffectId;
        if (write(mFd, &ev, sizeof(ev)) != sizeof(ev)) {
            PLOG(ERROR) << "EV_FF write";
            return false;
        }
        return true;
    }

    ndk::ScopedAStatus start(int ms, const std::shared_ptr<IVibratorCallback>& cb) {
        uint64_t gen;
        {
            std::lock_guard lock(mMutex);
            gen = ++mGeneration;
            if (!play(ms)) {
                return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
            }
        }
        if (cb) {
            std::thread([this, ms, cb, gen, self = ref<Vibrator>()] {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                {
                    std::lock_guard lock(mMutex);
                    if (mGeneration != gen) return;  // replaced or turned off
                }
                cb->onComplete();
            }).detach();
        }
        return ndk::ScopedAStatus::ok();
    }

    static constexpr int kDoubleGapMs = 80;

    ndk::ScopedAStatus startDouble(int ms, const std::shared_ptr<IVibratorCallback>& cb) {
        uint64_t gen;
        {
            std::lock_guard lock(mMutex);
            gen = ++mGeneration;
            if (!play(ms)) {
                return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
            }
        }
        std::thread([this, ms, cb, gen, self = ref<Vibrator>()] {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms + kDoubleGapMs));
            {
                std::lock_guard lock(mMutex);
                if (mGeneration != gen) return;
                play(ms);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            {
                std::lock_guard lock(mMutex);
                if (mGeneration != gen) return;
            }
            if (cb) cb->onComplete();
        }).detach();
        return ndk::ScopedAStatus::ok();
    }

    std::mutex mMutex;
    int mFd;
    int16_t mEffectId = -1;
    uint64_t mGeneration = 0;
};

}  // namespace aidl::android::hardware::vibrator

using aidl::android::hardware::vibrator::Vibrator;

int main(int argc, char** argv) {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto vib = ndk::SharedRefBase::make<Vibrator>();
    if (argc > 1) {  // manual test: <binary> <ms>
        int ms = atoi(argv[1]);
        if (!vib->on(ms, nullptr).isOk()) return EXIT_FAILURE;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms + 50));
        return EXIT_SUCCESS;
    }
    const std::string instance = std::string() + Vibrator::descriptor + "/default";
    CHECK_EQ(AServiceManager_addService(vib->asBinder().get(), instance.c_str()), STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
