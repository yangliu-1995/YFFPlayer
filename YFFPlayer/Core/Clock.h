#pragma once

#include <memory>

namespace yffplayer {
class Clock {
public:
    Clock() = default;
    ~Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;
    
    void init();
    double get();
    void set(double pts);
    void setAt(double pts, double time);
    void setSpeed(double speed);
    double getSpeed() const;
    void setPaused(bool paused);
private:
    std::atomic<double> mPts = 0.0;         // 当前播放时间戳（秒）
    std::atomic<double> mPtsDrift = 0.0;    // pts与系统时间的差值（秒）
    std::atomic<double> mLastUpdated = 0.0; // 上次更新时间（秒）
    std::atomic<double> mSpeed = 1.0;       // 播放速度（1.0为正常速度）
    std::atomic<bool> mPaused = false;      // 暂停状态
};
} // yffplayer
