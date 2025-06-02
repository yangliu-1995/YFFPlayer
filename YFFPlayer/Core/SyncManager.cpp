#include "SyncManager.h"
#include <chrono>
#include <cmath>

namespace {
constexpr double kMaxDropDiff = 0.050; // 50ms，转换为秒
}

namespace yffplayer {

SyncManager::SyncManager() {
    mClock.mPts = 0.0;
    mClock.mPtsDrift = 0.0;
    mClock.mLastUpdated = 0.0;
    mClock.mSpeed = 1.0;
    mClock.mPaused = false;
    mSyncMode = SyncMode::AUDIO;
}

void SyncManager::setSpeed(float speed) {
    std::lock_guard<std::mutex> lock(mClockMutex);
    mClock.mSpeed = (speed > 0.1f) ? speed : 0.1f; // 防止除零
}

float SyncManager::getSpeed() const {
    std::lock_guard<std::mutex> lock(mClockMutex);
    return static_cast<float>(mClock.mSpeed);
}

void SyncManager::updateClock(int64_t pts, int64_t duration) {
    if (mSyncMode.load() != SyncMode::EXTERNAL_CLOCK) {
        std::lock_guard<std::mutex> lock(mClockMutex);
        double ptsSec = pts / 1000.0; // 毫秒转秒
        double durationSec = duration / 1000.0;
        mClock.mPts = ptsSec + durationSec / mClock.mSpeed;
        mClock.mLastUpdated = getCurrentExternalTime();
        mClock.mPtsDrift = mClock.mPts - mClock.mLastUpdated;
    }
}

int64_t SyncManager::calculateDelay(int64_t pts, bool& shouldDropFrame) {
    double ptsSec = pts / 1000.0; // 毫秒转秒
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
//        std::lock_guard<std::mutex> lock(mClockMutex);
        double clockPts = getExternalClock() / 1000.0; // 毫秒转秒
        double diff = ptsSec - clockPts;
        
        // 考虑播放速度调整丢帧阈值
        shouldDropFrame = diff <= -kMaxDropDiff * mClock.mSpeed;
        if (shouldDropFrame) {
            return static_cast<int64_t>(diff * 1000.0); // 秒转毫秒
        }
        // 延迟时间也需要根据播放速度调整
        double adjustedDiff = diff / mClock.mSpeed;
        return adjustedDiff > 0 ? static_cast<int64_t>(adjustedDiff * 1000.0) : 0;
    } else {
        std::lock_guard<std::mutex> lock(mClockMutex);
        double diff = ptsSec - mClock.mPts;
        double adjustedDiff = diff / mClock.mSpeed;
        shouldDropFrame = diff < -kMaxDropDiff * mClock.mSpeed;
        return adjustedDiff >= 0 ? static_cast<int64_t>(adjustedDiff * 1000.0) : 0;
    }
}

int64_t SyncManager::getClock() const {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        return getExternalClock();
    }
    std::lock_guard<std::mutex> lock(mClockMutex);
    return static_cast<int64_t>(mClock.mPts * 1000.0); // 秒转毫秒
}

void SyncManager::setSyncMode(SyncMode mode) {
    mSyncMode = mode;
    if (mode == SyncMode::EXTERNAL_CLOCK) {
        startExternalClock();
    }
}

SyncMode SyncManager::getSyncMode() const {
    return mSyncMode.load();
}

void SyncManager::startExternalClock() {
    std::lock_guard<std::mutex> lock(mClockMutex);
    mExternalClockBaseTime = std::chrono::steady_clock::now();
    mClock.mPts = 0.0;
    mClock.mPtsDrift = 0.0;
    mClock.mLastUpdated = getCurrentExternalTime();
    mClock.mPaused = false;
}

void SyncManager::pauseExternalClock() {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        std::lock_guard<std::mutex> lock(mClockMutex);
        if (!mClock.mPaused.load()) {
            mClock.mPts = (getCurrentExternalTime() - mClock.mPtsDrift) / mClock.mSpeed;
            mClock.mLastUpdated = getCurrentExternalTime();
            mClock.mPaused = true;
        }
    }
}

void SyncManager::resumeExternalClock() {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        std::lock_guard<std::mutex> lock(mClockMutex);
        if (mClock.mPaused.load()) {
            mExternalClockBaseTime = std::chrono::steady_clock::now();
            mClock.mLastUpdated = getCurrentExternalTime();
            mClock.mPtsDrift = mClock.mLastUpdated - mClock.mPts * mClock.mSpeed;
            mClock.mPaused = false;
        }
    }
}

void SyncManager::seekExternalClock(int64_t positionMs) {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        std::lock_guard<std::mutex> lock(mClockMutex);
        mExternalClockBaseTime = std::chrono::steady_clock::now();
        mClock.mPts = positionMs / 1000.0; // 毫秒转秒
        mClock.mLastUpdated = getCurrentExternalTime();
        mClock.mPtsDrift = mClock.mLastUpdated - mClock.mPts * mClock.mSpeed;
    }
}

int64_t SyncManager::getExternalClock() const {
    if (mSyncMode.load() != SyncMode::EXTERNAL_CLOCK) {
        std::lock_guard<std::mutex> lock(mClockMutex);
        return static_cast<int64_t>(mClock.mPts * 1000.0);
    }
    std::lock_guard<std::mutex> lock(mClockMutex);
    if (mClock.mPaused.load()) {
        return static_cast<int64_t>(mClock.mPts * 1000.0);
    }
    return static_cast<int64_t>((getCurrentExternalTime() - mClock.mPtsDrift) / mClock.mSpeed * 1000.0);
}

void SyncManager::updateDriftWithPts(int64_t pts) {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        std::lock_guard<std::mutex> lock(mClockMutex);
        double currentTime = getCurrentExternalTime();
        double ptsSec = pts / 1000.0;
        mClock.mPtsDrift = currentTime - ptsSec;
        mClock.mLastUpdated = currentTime;
        mClock.mPts = ptsSec;
    }
}

double SyncManager::getCurrentExternalTime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - mExternalClockBaseTime).count() / 1000.0;
}

} // namespace yffplayer
