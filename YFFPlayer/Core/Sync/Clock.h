#pragma once

namespace yffplayer {
class Clock {
public:
    Clock() = default;
    ~Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;
    
    virtual void init() = 0;
    virtual double get() = 0;
    virtual void set(double pts, double duration) = 0;
    virtual void setAt(double pts, double duration, double time) = 0;
    virtual void setSpeed(double speed) = 0;
    virtual double getSpeed() const = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void update(double time) = 0;
};
} // yffplayer
