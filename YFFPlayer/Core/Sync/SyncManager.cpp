#include "SyncManager.h"

#include <algorithm>
#include <iostream>

#include "Clock.h"
#include "Log.h"

extern "C" {
#include <libavutil/macros.h>
#include <libavutil/time.h>
}

#define AV_NOSYNC_THRESHOLD 10.0

#define AV_SYNC_THRESHOLD_MIN 0.04

#define AV_SYNC_THRESHOLD_MAX 0.1

#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1

namespace yffplayer {
SyncManager::SyncManager(SyncType type) : mType(type) {
    mAudioClock = std::make_shared<Clock>();
    mExternalClock = std::make_shared<Clock>();
    mVideoClock = std::make_shared<Clock>();
}

SyncManager::~SyncManager() {}

void SyncManager::initAudioClock(double pts) {
    if (mAudioClock->isNAN()) {
        mAudioClock->set(pts);
    }
    if (mExternalClock->isNAN()) {
        mExternalClock->set(pts);
    }
}

void SyncManager::initVideoClock(double pts) {
    if (mVideoClock->isNAN()) {
        mVideoClock->set(pts);
    }
    if (mExternalClock->isNAN()) {
        mExternalClock->set(pts);
    }
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

double SyncManager::getSpeed() const { return mExternalClock->getSpeed(); }

double SyncManager::computeVideoTargetDelay(double delay) {
    double videoTime = mVideoClock->get();
    double masterTime = getClockTime();
    double diff = videoTime - masterTime;
    LogInfo << "compute video delay, video time: " << videoTime << ", master time:" << masterTime
            << ", diff: " << diff << ", delay: " << delay << ", fixed delay: " << delay + diff
            << std::endl;
    delay += diff;
    return delay;
}

double SyncManager::computeAudioTargetDelay(double pts) { return getClockTime() - pts; }

void SyncManager::updateVideoTime(double pts) {
    mVideoClock->set(pts);
    syncClockToSlave(mExternalClock, mVideoClock);
}

void SyncManager::syncClockToSlave(std::shared_ptr<Clock> clock,
                                   std::shared_ptr<Clock> slaveClock) {
    double time = clock->get();
    double slaveTime = slaveClock->get();
    if (fabs(time - slaveTime) > AV_NOSYNC_THRESHOLD) {
        clock->set(slaveTime);
    }
}

void SyncManager::updateAudioTime(double pts, double duration) {
    mAudioClock->set(pts + duration);
    syncClockToSlave(mExternalClock, mAudioClock);
}

double SyncManager::getClockTime() const {
    std::lock_guard<std::mutex> lock(mMutex);
    double clockTime = getMasterClock()->get();
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
            return mExternalClock;  // Default to external clock for unknown types
    }
}

}  // namespace yffplayer
