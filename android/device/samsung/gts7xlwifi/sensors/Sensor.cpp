/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "sensors-impl/Sensor.h"

#include <android-base/properties.h>
#include <log/log.h>
#include "SscStream.h"
#include "utils/SystemClock.h"

#include <cmath>
#include <sched.h>

#include <map>

// One SSC stream per data type, shared by the sensors using it
static std::shared_ptr<SscStream> sscStream(const char* type, float rate) {
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<SscStream>> streams;
    std::lock_guard l(lock);
    auto& s = streams[type];
    if (!s) s = std::make_shared<SscStream>(type, rate);
    return s;
}

using ::ndk::ScopedAStatus;

namespace aidl {
namespace android {
namespace hardware {
namespace sensors {

static constexpr int32_t kDefaultMaxDelayUs = 10 * 1000 * 1000;

Sensor::Sensor(ISensorsEventCallback* callback)
    : mIsEnabled(false),
      mSamplingPeriodNs(0),
      mLastSampleTimeNs(0),
      mCallback(callback),
      mMode(OperationMode::NORMAL) {
    mRunThread = std::thread(startThread, this);
}

Sensor::~Sensor() {
    std::unique_lock<std::mutex> lock(mRunMutex);
    mStopThread = true;
    mIsEnabled = false;
    mWaitCV.notify_all();
    lock.release();
    mRunThread.join();
}

const SensorInfo& Sensor::getSensorInfo() const {
    return mSensorInfo;
}

void Sensor::batch(int64_t samplingPeriodNs) {
    // gts7xl: on-change sensors are polled; don't spin
    if ((mSensorInfo.flags & static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE)) &&
        samplingPeriodNs < 100 * 1000 * 1000)
        samplingPeriodNs = 100 * 1000 * 1000;
    if (samplingPeriodNs < mSensorInfo.minDelayUs * 1000LL) {
        samplingPeriodNs = mSensorInfo.minDelayUs * 1000LL;
    } else if (samplingPeriodNs > mSensorInfo.maxDelayUs * 1000LL) {
        samplingPeriodNs = mSensorInfo.maxDelayUs * 1000LL;
    }

    if (mSamplingPeriodNs != samplingPeriodNs) {
        mSamplingPeriodNs = samplingPeriodNs;
        // Wake up the 'run' thread to check if a new event should be generated now
        mWaitCV.notify_all();
    }
}

void Sensor::activate(bool enable) {
    if (mIsEnabled != enable) {
        std::unique_lock<std::mutex> lock(mRunMutex);
        mIsEnabled = enable;
        mWaitCV.notify_all();
    }
}

ScopedAStatus Sensor::flush() {
    // Only generate a flush complete event if the sensor is enabled and if the sensor is not a
    // one-shot sensor.
    if (!mIsEnabled ||
        (mSensorInfo.flags & static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ONE_SHOT_MODE))) {
        return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(BnSensors::ERROR_BAD_VALUE));
    }

    // Note: If a sensor supports batching, write all of the currently batched events for the sensor
    // to the Event FMQ prior to writing the flush complete event.
    Event ev;
    ev.sensorHandle = mSensorInfo.sensorHandle;
    ev.sensorType = SensorType::META_DATA;
    EventPayload::MetaData meta = {
            .what = MetaDataEventType::META_DATA_FLUSH_COMPLETE,
    };
    ev.payload.set<EventPayload::Tag::meta>(meta);
    std::vector<Event> evs{ev};
    mCallback->postEvents(evs, isWakeUpSensor());

    return ScopedAStatus::ok();
}

void Sensor::startThread(Sensor* sensor) {
    sensor->run();
}

void Sensor::run() {
    std::unique_lock<std::mutex> runLock(mRunMutex);
    constexpr int64_t kNanosecondsInSeconds = 1000 * 1000 * 1000;

    struct sched_param params;
    // Set the thread to the lowest real-time priority.
    params.sched_priority = 1;
    if (sched_setscheduler(/*pid=*/0, SCHED_FIFO, &params)) {
        ALOGE("Unable to set SCHED_FIFO: error %s", strerror(errno));
    }
    while (!mStopThread) {
        if (!mIsEnabled || mMode == OperationMode::DATA_INJECTION) {
            mWaitCV.wait(runLock, [&] {
                return ((mIsEnabled && mMode == OperationMode::NORMAL) || mStopThread);
            });
        } else {
            timespec curTime;
            clock_gettime(CLOCK_BOOTTIME, &curTime);
            int64_t now = (curTime.tv_sec * kNanosecondsInSeconds) + curTime.tv_nsec;
            int64_t nextSampleTime = mLastSampleTimeNs + mSamplingPeriodNs;

            if (now >= nextSampleTime) {
                mLastSampleTimeNs = now;
                nextSampleTime = mLastSampleTimeNs + mSamplingPeriodNs;
                mCallback->postEvents(readEvents(), isWakeUpSensor());
            }

            mWaitCV.wait_for(runLock, std::chrono::nanoseconds(nextSampleTime - now));
        }
    }
}

bool Sensor::isWakeUpSensor() {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_WAKE_UP);
}

std::vector<Event> Sensor::readEvents() {
    std::vector<Event> events;
    Event event;
    event.sensorHandle = mSensorInfo.sensorHandle;
    event.sensorType = mSensorInfo.type;
    event.timestamp = ::android::elapsedRealtimeNano();
    memset(&event.payload, 0, sizeof(event.payload));
    readEventPayload(event.payload);
    events.push_back(event);
    return events;
}

void Sensor::setOperationMode(OperationMode mode) {
    if (mMode != mode) {
        std::unique_lock<std::mutex> lock(mRunMutex);
        mMode = mode;
        mWaitCV.notify_all();
    }
}

bool Sensor::supportsDataInjection() const {
    return mSensorInfo.flags & static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_DATA_INJECTION);
}

ScopedAStatus Sensor::injectEvent(const Event& event) {
    if (event.sensorType == SensorType::ADDITIONAL_INFO) {
        return ScopedAStatus::ok();
        // When in OperationMode::NORMAL, SensorType::ADDITIONAL_INFO is used to push operation
        // environment data into the device.
    }

    if (!supportsDataInjection()) {
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    if (mMode == OperationMode::DATA_INJECTION) {
        mCallback->postEvents(std::vector<Event>{event}, isWakeUpSensor());
        return ScopedAStatus::ok();
    }

    return ScopedAStatus::fromServiceSpecificError(
            static_cast<int32_t>(BnSensors::ERROR_BAD_VALUE));
}

OnChangeSensor::OnChangeSensor(ISensorsEventCallback* callback)
    : Sensor(callback), mPreviousEventSet(false) {}

void OnChangeSensor::activate(bool enable) {
    Sensor::activate(enable);
    if (!enable) {
        mPreviousEventSet = false;
    }
}

std::vector<Event> OnChangeSensor::readEvents() {
    std::vector<Event> events = Sensor::readEvents();
    std::vector<Event> outputEvents;

    for (auto iter = events.begin(); iter != events.end(); ++iter) {
        Event ev = *iter;
        if (!mPreviousEventSet ||
            memcmp(&mPreviousEvent.payload, &ev.payload, sizeof(ev.payload)) != 0) {
            outputEvents.push_back(ev);
            mPreviousEvent = ev;
            mPreviousEventSet = true;
        }
    }
    return outputEvents;
}

// Sensor frame -> Android axes, "x,y,z" with signs, e.g. "y,-x,z"
// (vendor.sensors.axes, read once). Default: the mount measured on the tablet.
static void remap(const std::array<float, 3>& s, float out[3]) {
    static const std::string spec = ::android::base::GetProperty("vendor.sensors.axes", "-y,x,z");
    size_t pos = 0;
    for (int i = 0; i < 3; i++) {
        size_t end = spec.find(',', pos);
        std::string t = spec.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? spec.size() : end + 1;
        float sign = 1;
        if (!t.empty() && t[0] == '-') {
            sign = -1;
            t = t.substr(1);
        }
        int axis = t == "y" ? 1 : t == "z" ? 2 : 0;
        out[i] = sign * s[axis];
    }
}

SscSensor::SscSensor(int32_t sensorHandle, ISensorsEventCallback* callback, const Spec& spec)
    : OnChangeSensor(callback), mSpec(spec), mOnChange(spec.minDelayUs == 0) {
    mSensorInfo.sensorHandle = sensorHandle;
    mSensorInfo.name = spec.name;
    mSensorInfo.vendor = "gts7xl SSC";
    mSensorInfo.version = 1;
    mSensorInfo.type = spec.type;
    mSensorInfo.typeAsString = "";
    mSensorInfo.maxRange = spec.maxRange;
    mSensorInfo.resolution = spec.resolution;
    mSensorInfo.power = spec.power;
    // On-change sensors are polled at 10 Hz and report only changes
    mSensorInfo.minDelayUs = mOnChange ? 0 : spec.minDelayUs;
    mSensorInfo.maxDelayUs = kDefaultMaxDelayUs;
    mSensorInfo.fifoReservedEventCount = 0;
    mSensorInfo.fifoMaxEventCount = 0;
    mSensorInfo.requiredPermission = "";
    mSensorInfo.flags = mOnChange ? static_cast<uint32_t>(SensorInfo::SENSOR_FLAG_BITS_ON_CHANGE_MODE)
                                  : 0;
    mStream = sscStream(spec.sscType, spec.rateHz);
    if (mOnChange) mSamplingPeriodNs = 100 * 1000 * 1000;
}

void SscSensor::activate(bool enable) {
    if (enable == mIsEnabled) return;
    if (enable)
        mStream->acquire();
    else
        mStream->release();
    if (mOnChange) {
        OnChangeSensor::activate(enable);
        mSamplingPeriodNs = 100 * 1000 * 1000;
    } else {
        Sensor::activate(enable);
    }
}

std::vector<Event> SscSensor::readEvents() {
    std::array<float, 3> v;
    int acc;
    if (!mStream->latest(&v, &acc)) return {};  // nothing streamed yet
    return mOnChange ? OnChangeSensor::readEvents() : Sensor::readEvents();
}

void SscSensor::readEventPayload(EventPayload& payload) {
    std::array<float, 3> v{};
    int acc = 0;
    mStream->latest(&v, &acc);
    if (mSpec.type == SensorType::LIGHT) {
        payload.set<EventPayload::Tag::scalar>(v[0] * mSpec.scale);
        return;
    }
    float o[3] = {v[0], v[1], v[2]};
    if (mSpec.remap) remap(v, o);
    // SSC accuracy 0..3 matches Android's SensorStatus
    EventPayload::Vec3 vec3 = {
            .x = o[0] * mSpec.scale,
            .y = o[1] * mSpec.scale,
            .z = o[2] * mSpec.scale,
            .status = static_cast<SensorStatus>(acc < 0 ? 0 : acc > 3 ? 3 : acc),
    };
    payload.set<EventPayload::Tag::vec3>(vec3);
}

}  // namespace sensors
}  // namespace hardware
}  // namespace android
}  // namespace aidl
