#include "Clock.h"

#include <iostream>

extern "C" {
#include <libavutil/time.h>
}

namespace yffplayer {
double Clock::get() {
    if (mPaused) {
        return mPts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        double ct = mPtsDrift + time - (time - mLastUpdated) * (1 - mSpeed);
        std::cerr<<"drift: " << mPtsDrift << ", ct: " << ct << ", time: " << time << ", lastupdated: " << mLastUpdated << std::endl;
        return ct;
    }
}

void Clock::set(double pts) {
    double time = av_gettime_relative() / 1000000.0;
    setAt(pts, time);
}

void Clock::setAt(double pts, double time) {
    mPts=pts;
    mLastUpdated = time;
    mPtsDrift = mPts - time;
}

void Clock::setSpeed(double speed) {
    set(get());
    mSpeed = speed;
}

double Clock::getSpeed() const {
    return mSpeed.load();
}

void Clock::init() {
    double time = av_gettime_relative() / 1000000.0;
    setAt(0.0, time);
}

void Clock::setPaused(bool paused) {
    mPaused = paused;
}
} // namespace yffplayer
