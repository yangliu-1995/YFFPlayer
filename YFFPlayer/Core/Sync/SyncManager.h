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
    SyncManager(SyncType type);
    ~SyncManager();

    void pause();

    void resume();

    void setSpeed(double speed);

    double getSpeed() const;

    double computeAudioTargetDelay(double pts);

    double computeVideoTargetDelay(double delay);

    void updateAudioTime(double pts, double duration);

    void updateVideoTime(double pts);

    void updateClock(double pts);

    double getClockTime() const;

private:
    void syncClockToSlave(std::shared_ptr<Clock> clock, std::shared_ptr<Clock> slaveClock);
    std::shared_ptr<Clock> getMasterClock() const;
    SyncType mType { SyncType::Audio };
    std::shared_ptr<Clock> mAudioClock;
    std::shared_ptr<Clock> mExternalClock;
    std::shared_ptr<Clock> mVideoClock;
};
}  // namespace yffplayer
