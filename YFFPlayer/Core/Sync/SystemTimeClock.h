#pragma once

#include "Clock.h"

extern "C" {
#include <libavutil/time.h>
}

namespace yffplayer {
class SystemTimeClock: public Clock {
public:
    void init() override {};
    double get() override {
        if (mPaused) {
            return mPts;
        } else {
            double time = av_gettime_relative() / 1000000.0;
            double ct = mPtsDrift + time - (time - mLastUpdated) * (1 - mSpeed);
            std::cerr<<"drift: " << mPtsDrift * 1000 << ", ct: " << ct << ", time: " << time * 1000 << ", lastupdated: " << mLastUpdated * 1000 << ", 1 - mSpeed: " << 1 - mSpeed << std::endl;
//            mLastUpdated = time;
            return ct;
        }
    };
    void set(double pts, double duration) override {
        double time = av_gettime_relative() / 1000000.0;
        setAt(pts, duration, time);
    };
    void setAt(double pts, double duration, double time) override {
        mPts=pts;
        mLastUpdated = time;
        mPtsDrift = mPts - time;
        std::cerr<<"set time at pts: " << pts * 1000 << ", drift: " << (mPts - time) * 1000 << ", time: " << time * 1000 << std::endl;
    };
    void setSpeed(double speed) override {
        set(get(), 0);
        mSpeed = speed;
    };
    double getSpeed() const override {
        return mSpeed;
    };
    void setPaused(bool paused) override {
        set(get(), 0);
        mPaused = paused;
    };
    void update(double time) override {
        mLastUpdated = av_gettime_relative() / 1000000.0;
    };
private:
    std::atomic<double> mPts = 0.0; 
    std::atomic<double> mPtsDrift {0.0};    // pts与系统时间的差值（秒）
    std::atomic<double> mLastUpdated {0.0}; // 上次更新时间（秒）
    std::atomic<double> mSpeed {1.0};       // 播放速度（1.0为正常速度）
    std::atomic<bool> mPaused {false};      // 暂停状态
};
} // namespace yffplayer
