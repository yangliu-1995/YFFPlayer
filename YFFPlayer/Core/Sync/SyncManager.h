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

    double computeAudioFrameDelay(double pts);

    double computeVideoFrameDelay(double pts);
    
    double getClock();
    
    void updateTime(double pts);

    void updateAudioTime(double pts, double duration);

    void updateClock(double pts);

    std::atomic<double> mAudioClock{0};
private:
    SyncType mType { SyncType::External };  // 改为External模式
    std::unique_ptr<Clock> mClock;
};
}  // namespace yffplayer
