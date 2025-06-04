#pragma once

#include "Clock.h"

#include <atomic>
#include <iostream>

namespace yffplayer {
class AudioClock: public Clock {
public:
    void init() override {};
    double get() override {
        return mTime;
    };
    void set(double pts, double duration) override {
        setAt(pts, duration, 0);
    };
    void setAt(double pts, double duration, double time) override {
        mTime = pts + duration / mSpeed;
        std::cerr<< "update time pts: " << pts << ", duration: " << duration << ", time:" << mTime << std::endl;
    };
    void setSpeed(double speed) override {
        mSpeed = speed;
    };
    double getSpeed() const override {
        return mSpeed;
    };
    void setPaused(bool paused) override {};
    void update(double time) override {};
private:
    std::atomic<double> mTime{0};
    std::atomic<float> mSpeed{1};

};
} // yffplayer
