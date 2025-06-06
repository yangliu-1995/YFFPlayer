#pragma once

#include <atomic>

extern "C" {
#include <libavutil/time.h>
}

namespace yffplayer {
class Clock {
public:
    void init() {};

    double get() {
        if (mPaused) {
            return mPts;
        } else {
            double time = av_gettime_relative() / 1000000.0;
            double ct = mPtsDrift + time - (time - mLastUpdated) * (1 - mSpeed);
            return ct;
        }
    };

    void set(double pts) {
        double time = av_gettime_relative() / 1000000.0;
        setAt(pts, time);
    };

    void setAt(double pts, double time) {
        mPts = pts;
        mLastUpdated = time;
        mPtsDrift = mPts - time;
    };

    void setSpeed(double speed) {
        set(get());
        mSpeed = speed;
    };

    double getSpeed() const { return mSpeed; };

    void setPaused(bool paused) {
        set(get());
        mPaused = paused;
    };

    bool isNAN() const { return isnan(mPts); }

private:
    std::atomic<double> mPts{NAN};
    std::atomic<double> mPtsDrift{0.0};     // pts与系统时间的差值（秒）
    std::atomic<double> mLastUpdated{NAN};  // 上次更新时间（秒）
    std::atomic<double> mSpeed{1.0};        // 播放速度（1.0为正常速度）
    std::atomic<bool> mPaused{false};       // 暂停状态
};
}  // namespace yffplayer
