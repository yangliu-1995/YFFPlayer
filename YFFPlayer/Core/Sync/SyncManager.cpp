#include "SyncManager.h"

#include <algorithm>

#include "Clock.h"
#include "Log.h"

extern "C" {
#include <libavutil/macros.h>
#include <libavutil/time.h>
}

namespace {
constexpr double kAVNosyncThreshold = 10.0;
constexpr double kAVSyncThresholdMin = 0.04;
constexpr double kAVSyncThresholdMax = 0.1;
constexpr double kAVSyncFramedupThreshold = 0.1;
}

namespace yffplayer {
SyncManager::SyncManager(SyncType type) : type_(type) {
    audioClock_ = std::make_shared<Clock>();
    externalClock_ = std::make_shared<Clock>();
    videoClock_ = std::make_shared<Clock>();
}

SyncManager::~SyncManager() {}

void SyncManager::initAudioClock(double pts) {
    if (audioClock_->isNAN()) {
        audioClock_->set(pts);
    }
    if (externalClock_->isNAN()) {
        externalClock_->set(pts);
    }
}

void SyncManager::initVideoClock(double pts) {
    if (videoClock_->isNAN()) {
        videoClock_->set(pts);
    }
    if (externalClock_->isNAN()) {
        externalClock_->set(pts);
    }
}

void SyncManager::pause() {
    audioClock_->setPaused(true);
    externalClock_->setPaused(true);
    videoClock_->setPaused(true);
}

void SyncManager::resume() {
    audioClock_->setPaused(false);
    externalClock_->setPaused(false);
    videoClock_->setPaused(false);
}

void SyncManager::setSpeed(double speed) {
    audioClock_->setSpeed(speed);
    externalClock_->setSpeed(speed);
    videoClock_->setSpeed(speed);
}

double SyncManager::getSpeed() const { return externalClock_->getSpeed(); }

double SyncManager::computeVideoTargetDelay(double delay) {
    double videoTime = videoClock_->get();
    double masterTime = getClockTime();
    double diff = videoTime - masterTime;

    double originalDelay = delay;
    double sync_threshold = FFMAX(kAVSyncThresholdMin, FFMIN(kAVSyncThresholdMax, delay));
    if (!isnan(diff) && fabs(diff) < maxFrameDuration_) {
        if (diff <= -sync_threshold)
            delay = FFMAX(0, delay + diff);
        else if (diff >= sync_threshold && delay > kAVSyncFramedupThreshold)
            delay = delay + diff;
        else if (diff >= sync_threshold)
            delay = 2 * delay;
    }

    LogInfo << "compute video delay, video time: " << videoTime << ", master time:" << masterTime
            << ", diff: " << diff << ", delay: " << originalDelay << ", fixed delay: " << delay;
    return delay;
}

double SyncManager::computeAudioTargetDelay(double pts) {
    double audioTime = audioClock_->get();
    double masterTime = getClockTime();
    double videoTime = videoClock_->get();
    double diff = audioTime - masterTime;
    LogInfo << "compute audio delay, audio time: " << audioTime
            << ", master time: " << masterTime << ", video time: " << videoTime
            << ", diff: " << diff
            << ", av diff: " << audioTime - videoTime;
    return getClockTime() - pts;
}

double SyncManager::getAudioDiff() const {
    double audioTime = audioClock_->get();
    double masterTime = getClockTime();
    return audioTime - masterTime;
}

void SyncManager::updateVideoTime(double pts) {
    videoClock_->set(pts);
    syncClockToSlave(externalClock_, videoClock_);
}

void SyncManager::syncClockToSlave(std::shared_ptr<Clock> clock,
                                   std::shared_ptr<Clock> slaveClock) {
    double time = clock->get();
    double slaveTime = slaveClock->get();
    if (!slaveClock->isNAN() && (clock->isNAN() || fabs(time - slaveTime) > kAVNosyncThreshold)) {
        clock->set(slaveTime);
    }
}

void SyncManager::updateAudioTime(double pts, double duration) {
    audioClock_->set(pts + duration);
    syncClockToSlave(externalClock_, audioClock_);
}

double SyncManager::getClockTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double clockTime = getMasterClock()->get();
    return clockTime;
}

std::shared_ptr<Clock> SyncManager::getMasterClock() const {
    switch (type_) {
        case SyncType::Audio:
            return audioClock_;
        case SyncType::External:
            return externalClock_;
        case SyncType::Video:
            return videoClock_;
        default:
            return externalClock_;  // Default to external clock for unknown types
    }
}

void SyncManager::setMaxFrameDuration(float duration) { maxFrameDuration_ = duration; }

}  // namespace yffplayer
