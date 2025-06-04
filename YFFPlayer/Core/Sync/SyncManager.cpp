#include "SyncManager.h"
#include <algorithm>
#include <iostream>

#include "AudioClock.h"
#include "SystemTimeClock.h"

extern "C" {
#include <libavutil/time.h>
}

#define AV_NOSYNC_THRESHOLD 10.0

namespace yffplayer {
SyncManager::SyncManager(SyncType type): mType(type) {
    switch (type) {
        case SyncType::Audio:
            mClock = std::make_unique<AudioClock>();
            break;
        default:
            mClock = std::make_unique<SystemTimeClock>();
            break;
    }
}

SyncManager::~SyncManager() {
}

void SyncManager::pause() {
    mClock->setPaused(true);
}

void SyncManager::resume() {
    mClock->setPaused(false);
}

void SyncManager::setSpeed(double speed) {
    mClock->setSpeed(speed);
}

double SyncManager::getSpeed() const {
    return mClock->getSpeed();
}

void SyncManager::updateTime(double pts) {
    if (mType == SyncType::Audio) {
        return;
    }
//    mClock->update(pts);
    mClock->set(pts, 0);
}

void SyncManager::updateClock(double pts) {
    if (mType == SyncType::Audio) {
        return;
    }
    mClock->set(pts, 0);
}

void SyncManager::updateAudioTime(double pts, double duration) {
    mAudioClock = pts + duration / getSpeed();
    std::cerr<<"update audio time"<<mAudioClock<<std::endl;
    if (mType != SyncType::Audio) {
        return;
    }
    mClock->set(pts, duration);
}

double SyncManager::computeAudioFrameDelay(double pts) {
    if (mType == SyncType::Audio) {
        return 0;
    }
    double time = getClock();
    double delay = mAudioClock - time;
    std::cerr << "audio clock: " << mAudioClock << ", master clock: " << time << ", delay: " << delay << std::endl;
    return delay;
}

double SyncManager::computeVideoFrameDelay(double pts) {
    double time = getClock();
    double delay = (pts - time) / mClock -> getSpeed();

    return delay;
}

double SyncManager::getClock() {
    if (mType == SyncType::Audio) {
        return mAudioClock;
    }
    return mClock->get();
}

} // namespace yffplayer
