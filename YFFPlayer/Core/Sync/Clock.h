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
        if (paused_) {
            return pts_;
        } else {
            double time = av_gettime_relative() / 1000000.0;
            double ct = ptsDrift_ + time - (time - lastUpdated_) * (1 - speed_);
            return ct;
        }
    };

    void set(double pts) {
        double time = av_gettime_relative() / 1000000.0;
        setAt(pts, time);
    };

    void setAt(double pts, double time) {
        pts_ = pts;
        lastUpdated_ = time;
        ptsDrift_ = pts_ - time;
    };

    void setSpeed(double speed) {
        set(get());
        speed_ = speed;
    };

    double getSpeed() const { return speed_; };

    void setPaused(bool paused) {
        set(get());
        paused_ = paused;
    };

    bool isNAN() const { return isnan(pts_); }

private:
    std::atomic<double> pts_{NAN};
    std::atomic<double> ptsDrift_{0.0};     // pts与系统时间的差值（秒）
    std::atomic<double> lastUpdated_{NAN};  // 上次更新时间（秒）
    std::atomic<double> speed_{1.0};        // 播放速度（1.0为正常速度）
    std::atomic<bool> paused_{false};       // 暂停状态
};
}  // namespace yffplayer
