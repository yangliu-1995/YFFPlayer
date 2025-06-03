#pragma once

#include "Clock.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace yffplayer {
class SyncManager {
public:
    enum class SyncType {
        Audio = 0,
        External = 1,
        Video = 2,
    };
    SyncManager();
    ~SyncManager();

    void pause();

    void resume();

    void setSpeed(double speed);

    double getSpeed() const;

    double computeFrameDelay(double framePts);
    
    double getClockTime();
    
    void updateTime(double time);
    void updateVideoTime(double time);
    void updateAudioTime(double time);

private:
    SyncType type { SyncType::External };  // 改为External模式
    Clock *mAudioClock;
    Clock *mVideoClock;
    Clock *mExternalClock;
    std::atomic<bool> mPaused;
    Clock *getMasterClock() const;
    double getMasterClockTime() const;

    void syncClockToSlave(Clock *clock, Clock *slave);
};
}  // namespace yffplayer
