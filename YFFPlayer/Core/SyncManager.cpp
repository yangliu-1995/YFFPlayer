#include "SyncManager.h"
#include <algorithm>
#include <iostream>

extern "C" {
#include <libavutil/time.h>
}

#define AV_NOSYNC_THRESHOLD 10.0

namespace yffplayer {
SyncManager::SyncManager() {
    mAudioClock = new Clock();
    mExternalClock = new Clock();
    mVideoClock = new Clock();
    mAudioClock->init();
    mExternalClock->init();
    mVideoClock->init();
}

SyncManager::~SyncManager() {
    delete mAudioClock;
    delete mExternalClock;
    delete mVideoClock;
}

double SyncManager::getMasterClockTime() const {
    return getMasterClock()->get();
}

void SyncManager::pause() {
    mExternalClock->set(mExternalClock->get());
    mPaused = true;
    mAudioClock->setPaused(true);
    mExternalClock->setPaused(true);
    mVideoClock->setPaused(true);
}

void SyncManager::resume() {
    mExternalClock->set(mVideoClock->get());
    mPaused = false;
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
    return 0;
}

void SyncManager::syncClockToSlave(Clock *clock, Clock *slave) {
    double slaveClockTime = slave->get();
    clock->set(slaveClockTime);
}

void SyncManager::updateTime(double time) {
    getMasterClock()->set(time);
//    switch (type) {
//        case SyncType::Audio:
//            updateAudioTime(time);
//            break;
//        case SyncType::Video:
//            updateVideoTime(time);
//            break;
//        case SyncType::External:
//            mExternalClock->set(time);
//            break;
//    }
}

void SyncManager::updateVideoTime(double time) {
    mVideoClock->set(time);
    syncClockToSlave(mExternalClock, mVideoClock);
}

void SyncManager::updateAudioTime(double time) {
//    if (type == SyncType::Audio) {
//        getMasterClock()->set(time);
//    }
}

double SyncManager::computeFrameDelay(double framePts) {
    double masterClock = getMasterClockTime();
    double diff = framePts - masterClock;  // 修正计算方向
//    if (diff > 0.05) {
//        updateTime(framePts);
//    }
    std::cerr << "compute delay, framePts: " << framePts << ", master clock: " << masterClock << ", diff: " << diff << std::endl;
    return diff >= 0 ? diff : -diff;
}

Clock *SyncManager::getMasterClock() const {
//    switch (type) {
//        case SyncType::Audio:
//            return mAudioClock;
//            break;
//        case SyncType::External:
//            return mExternalClock;
//            break;
//        case SyncType::Video:
//            return mVideoClock;
//            break;
//        default:
//            break;
//    }
    return mExternalClock;
}

double SyncManager::getClockTime() {
    return getMasterClockTime();
}

} // namespace yffplayer
