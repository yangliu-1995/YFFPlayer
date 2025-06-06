#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "Clock.h"

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

    void initAudioClock(double pts);

    void initVideoClock(double pts);

    double computeAudioTargetDelay(double pts);

    double computeVideoTargetDelay(double delay);

    void updateAudioTime(double pts, double duration);

    void updateVideoTime(double pts);

    void updateClock(double pts);

    double getClockTime() const;

    void setMaxFrameDuration(float duration);

private:
    void syncClockToSlave(std::shared_ptr<Clock> clock, std::shared_ptr<Clock> slaveClock);

    std::shared_ptr<Clock> getMasterClock() const;

    SyncType type_{SyncType::Audio};

    std::shared_ptr<Clock> audioClock_;

    std::shared_ptr<Clock> externalClock_;

    std::shared_ptr<Clock> videoClock_;

    mutable std::mutex mutex_;

    std::atomic<float> maxFrameDuration_{10.0f};
};
}  // namespace yffplayer
