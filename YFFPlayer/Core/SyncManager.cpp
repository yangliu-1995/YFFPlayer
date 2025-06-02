#include "SyncManager.h"
#include <chrono>
#include <cmath>

namespace {
constexpr int kMaxDropDiff = 50;
}

namespace yffplayer {

// 原有接口实现
void SyncManager::setSpeed(float speed) { 
    mSpeed = speed; 
}

float SyncManager::getSpeed() const { 
    return mSpeed; 
}

void SyncManager::updateClock(int64_t pts, int64_t duration) {
    // 只有在非外部时钟模式下才更新内部时钟
    if (mSyncMode.load() != SyncMode::EXTERNAL_CLOCK) {
        mClock = pts + static_cast<int64_t>(duration / mSpeed.load());
    }
}

int64_t SyncManager::calculateDelay(int64_t pts, bool& shouldDropFrame) {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        // 外部时钟模式：使用 pts - getClock() 计算延迟
        int64_t currentClock = getClock();
        int64_t clockPts = getCurrentExternalTime() - mPtsDrift;  // 转换为秒
        int64_t diff = pts - clockPts;
        
        // 判断是否丢帧
        shouldDropFrame = diff <= -kMaxDropDiff;
        if (shouldDropFrame) {
            return diff;
        }
        // 返回延迟时间（毫秒）
        if (diff > 0) {
            return diff;
        } else {
            return 0;
        }
    } else {
        // 原有逻辑：音频/视频时钟
        int64_t diff = pts - mClock;
        int64_t adjustedDiff = static_cast<int64_t>(diff / mSpeed.load());
        shouldDropFrame = diff < -kMaxDropDiff;
        return adjustedDiff >= 0 ? adjustedDiff : 0;
    }
}

int64_t SyncManager::getClock() const {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        return getExternalClock();
    }
    return mClock.load();
}

// 新增同步模式接口
void SyncManager::setSyncMode(SyncMode mode) {
    mSyncMode = mode;
    if (mode == SyncMode::EXTERNAL_CLOCK) {
        startExternalClock();
    }
}

SyncMode SyncManager::getSyncMode() const {
    return mSyncMode.load();
}

// 外部时钟控制实现
void SyncManager::startExternalClock() {
    mExternalClockBaseTime = std::chrono::steady_clock::now();
    mExternalClockStartTime = 0;
    mExternalClockPauseOffset = 0;
    mExternalClockPaused = false;
    mPtsDrift = 0;  // 重置PTS漂移补偿
}

void SyncManager::pauseExternalClock() {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        if (!mExternalClockPaused.load()) {
            mExternalClockPauseOffset = getCurrentExternalTime();
            mExternalClockPaused = true;
        }
    }
}

void SyncManager::resumeExternalClock() {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        if (mExternalClockPaused.load()) {
            mExternalClockBaseTime = std::chrono::steady_clock::now();
            mExternalClockStartTime = mExternalClockPauseOffset.load();
            mExternalClockPaused = false;
        }
    }
}

void SyncManager::seekExternalClock(int64_t positionMs) {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        mExternalClockBaseTime = std::chrono::steady_clock::now();
        mExternalClockStartTime = positionMs;
        if (mExternalClockPaused.load()) {
            mExternalClockPauseOffset = positionMs;
        }
        mPtsDrift = 0;  // 重置PTS漂移补偿
    }
}

int64_t SyncManager::getExternalClock() const {
    if (mSyncMode.load() != SyncMode::EXTERNAL_CLOCK) {
        return mClock.load();
    }
    
    return getCurrentExternalTime() - mPtsDrift.load();
}

void SyncManager::updateDriftWithPts(int64_t pts) {
    if (mSyncMode.load() == SyncMode::EXTERNAL_CLOCK) {
        int64_t currentSystemTime = getCurrentExternalTime();
        mPtsDrift = currentSystemTime - pts;
    }
}

// 内部辅助方法
int64_t SyncManager::getCurrentExternalTime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

}  // namespace yffplayer
