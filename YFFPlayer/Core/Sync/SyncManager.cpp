#include "SyncManager.h"
#include <algorithm>
#include <iostream>

#include "Clock.h"

extern "C" {
#include <libavutil/time.h>
#include <libavutil/macros.h>
}

#define AV_NOSYNC_THRESHOLD 10.0

#define AV_SYNC_THRESHOLD_MIN 0.04

#define AV_SYNC_THRESHOLD_MAX 0.1

#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1

namespace yffplayer {
SyncManager::SyncManager(SyncType type): mType(type) {
    mAudioClock = std::make_shared<Clock>();
    mExternalClock = std::make_shared<Clock>();
    mVideoClock = std::make_shared<Clock>();
}

SyncManager::~SyncManager() {
}

void SyncManager::initAudioClock() {
    mAudioClock->set(0.0);
    mExternalClock->set(0.0);
}

void SyncManager::initVideoClock() {
    mVideoClock->set(0.0);
    mExternalClock->set(0.0);
}

void SyncManager::pause() {
    mAudioClock->setPaused(true);
    mExternalClock->setPaused(true);
    mVideoClock->setPaused(true);
}

void SyncManager::resume() {
    mAudioClock->setPaused(false);
    mExternalClock->setPaused(false);
    mVideoClock->setPaused(false);
}

void SyncManager::setSpeed(double speed) {
    mAudioClock->setSpeed(speed);
    mExternalClock->setSpeed(speed);
    mVideoClock->setSpeed(speed);
}

double SyncManager::getSpeed() const {
    return mExternalClock->getSpeed();
}

double SyncManager::computeVideoTargetDelay(double delay) {
    if (mType == SyncType::Video) {
        return delay;
    }
    double diff = mVideoClock->get() - getClockTime();
    delay += diff;
    return delay;
}

double SyncManager::computeAudioTargetDelay(double pts) {
    return getClockTime() - pts;
}

void SyncManager::updateVideoTime(double pts) {
    mVideoClock->set(pts);
    syncClockToSlave(mExternalClock, mVideoClock);
}

void SyncManager::syncClockToSlave(std::shared_ptr<Clock> clock, std::shared_ptr<Clock> slaveClock) {
    double time = clock->get();
    double slaveTime = slaveClock->get();
    if (fabs(time - slaveTime) > AV_NOSYNC_THRESHOLD) {
        clock->set(slaveTime);
    }
}

void SyncManager::updateAudioTime(double pts, double duration) {
    mAudioClock->set(pts + duration / getSpeed());
//    std::cerr << "Update audio time, pts: " << pts * 1000.0
//                << ", duration: " << duration * 1000.0
      std::cerr          << "Update audio time, master time: " << getClockTime() << std::endl;

    syncClockToSlave(mExternalClock, mAudioClock);
}

double SyncManager::getClockTime() const {
    std::lock_guard<std::mutex> lock(mMutex);
    double clockTime = getMasterClock()->get();
    if (clockTime > 1000000.0) {
        std::cerr << "Clock time is too large, resetting to 0" << std::endl;
    }
    return clockTime;
}

std::shared_ptr<Clock> SyncManager::getMasterClock() const {
    switch (mType) {
        case SyncType::Audio:
            return mAudioClock;
        case SyncType::External:
            return mExternalClock;
        case SyncType::Video:
            return mVideoClock;
        default:
            return mExternalClock; // Default to external clock for unknown types
    }
}

} // namespace yffplayer
