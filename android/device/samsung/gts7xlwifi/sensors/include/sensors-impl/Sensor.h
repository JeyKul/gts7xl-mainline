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

#include <memory>
#include <thread>

#include <aidl/android/hardware/sensors/BnSensors.h>

class SscStream;

namespace aidl {
namespace android {
namespace hardware {
namespace sensors {

class ISensorsEventCallback {
  public:
    using Event = ::aidl::android::hardware::sensors::Event;

    virtual ~ISensorsEventCallback(){};
    virtual void postEvents(const std::vector<Event>& events, bool wakeup) = 0;
};

class Sensor {
  public:
    using OperationMode = ::aidl::android::hardware::sensors::ISensors::OperationMode;
    using Event = ::aidl::android::hardware::sensors::Event;
    using EventPayload = ::aidl::android::hardware::sensors::Event::EventPayload;
    using SensorInfo = ::aidl::android::hardware::sensors::SensorInfo;
    using SensorType = ::aidl::android::hardware::sensors::SensorType;
    using MetaDataEventType =
            ::aidl::android::hardware::sensors::Event::EventPayload::MetaData::MetaDataEventType;

    Sensor(ISensorsEventCallback* callback);
    virtual ~Sensor();

    const SensorInfo& getSensorInfo() const;
    void batch(int64_t samplingPeriodNs);
    virtual void activate(bool enable);
    ndk::ScopedAStatus flush();

    void setOperationMode(OperationMode mode);
    bool supportsDataInjection() const;
    ndk::ScopedAStatus injectEvent(const Event& event);

  protected:
    void run();
    virtual std::vector<Event> readEvents();
    virtual void readEventPayload(EventPayload&) = 0;
    static void startThread(Sensor* sensor);

    bool isWakeUpSensor();

    bool mIsEnabled;
    int64_t mSamplingPeriodNs;
    int64_t mLastSampleTimeNs;
    SensorInfo mSensorInfo;

    std::atomic_bool mStopThread;
    std::condition_variable mWaitCV;
    std::mutex mRunMutex;
    std::thread mRunThread;

    ISensorsEventCallback* mCallback;

    OperationMode mMode;
};

class OnChangeSensor : public Sensor {
  public:
    OnChangeSensor(ISensorsEventCallback* callback);

    virtual void activate(bool enable) override;

  protected:
    virtual std::vector<Event> readEvents() override;

  protected:
    Event mPreviousEvent;
    bool mPreviousEventSet;
};

// gts7xl: an Android sensor fed by an SSC stream on the sensor DSP
class SscSensor : public OnChangeSensor {
  public:
    struct Spec {
        const char* sscType;   // SSC data type, e.g. "accel"
        float rateHz;          // stream rate (continuous sensors)
        SensorType type;
        const char* name;
        float maxRange, resolution, power;
        int32_t minDelayUs;    // 0: on-change
        bool remap;            // vector in the sensor frame: map to Android's axes
        float scale;           // SSC unit -> Android unit
    };
    SscSensor(int32_t sensorHandle, ISensorsEventCallback* callback, const Spec& spec);

    void activate(bool enable) override;

  protected:
    std::vector<Event> readEvents() override;
    void readEventPayload(EventPayload& payload) override;

  private:
    const Spec mSpec;
    std::shared_ptr<::SscStream> mStream;
    bool mOnChange;
};

}  // namespace sensors
}  // namespace hardware
}  // namespace android
}  // namespace aidl
