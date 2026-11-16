// gts7xl: one SSC (sensor DSP) data type streamed over QRTR with
// gts7xl-sensord's client. Runs while at least one user holds it and keeps
// the latest sample.
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

class SscStream {
  public:
    SscStream(std::string type, float rateHz);
    ~SscStream();

    void acquire();
    void release();
    // Latest sample (sensor frame); false until the first one arrived
    bool latest(std::array<float, 3>* v, int* accuracy);

    // for the C callback
    void onSample(const float* v, int n, int accuracy);

  private:
    void run();
    int streamOnce();

    const std::string mType;
    const float mRate;
    std::mutex mLock;
    std::condition_variable mCv;
    int mUsers = 0;
    bool mExit = false;
    volatile int mStop = 0;
    bool mValid = false;
    std::array<float, 3> mValue{};
    int mAccuracy = 0;
    std::thread mThread;
};
