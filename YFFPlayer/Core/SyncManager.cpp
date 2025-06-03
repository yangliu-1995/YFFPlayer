#include "SyncManager.h"
#include <algorithm>
#include <iostream>

namespace yffplayer {
SyncManager::SyncManager() {
    mClock.set(0.0);
}

void SyncManager::pause() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mClock.mPaused) {
        mClock.mPts = mClock.get();
        mClock.mPaused = true;
    }
}

void SyncManager::resume() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClock.mPaused) {
        mClock.mLastUpdatedUs = Clock::getCurrentTimeUs();
        mClock.mPaused = false;
    }
}

void SyncManager::seek(double pts) {
    std::lock_guard<std::mutex> lock(mMutex);
    mClock.set(pts, true);
}

void SyncManager::setPlaybackRate(double speed) {
    std::lock_guard<std::mutex> lock(mMutex);
    // 限制播放速率范围 [0.1, 10.0]
    if (speed < 0.1 || speed > 10.0) {
        std::cerr << "无效的播放速率: " << speed << "，限制为 [0.1, 10.0]\n";
        speed = std::clamp(speed, 0.1, 10.0);
    }
    mClock.setSpeed(speed);
}

double SyncManager::getSyncTime() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mClock.get();
}

void SyncManager::updatePtsDrift(double pts) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mClock.mPaused) {
        mClock.mPts = pts;
        mClock.mLastUpdatedUs = Clock::getCurrentTimeUs();
        mClock.mTimeOffset = pts - (mClock.mLastUpdatedUs / 1e6);
    }
}

double SyncManager::calcDelay(double pts, bool &shouldDrop) {
    std::lock_guard<std::mutex> lock(mMutex);
    static std::vector<double> delayHistory; // 记录历史延迟
    double syncTime = mClock.get();
    double delay = pts - syncTime;
    
    // 记录延迟历史，用于动态调整丢帧阈值
    delayHistory.push_back(delay);
    if (delayHistory.size() > 10) delayHistory.erase(delayHistory.begin());
    
    // 计算平均延迟
    double avgDelay = 0.0;
    for (double d : delayHistory) avgDelay += d;
    avgDelay /= delayHistory.size();
    
    // 动态丢帧阈值
    if (delay < -std::max(DROP_THRESHOLD, avgDelay * 1.5)) {
        shouldDrop = true;
        return 0.0;
    }
    
    shouldDrop = false;
    if (delay > MAX_DELAY) {
        delay = MAX_DELAY;
    }
    
    return delay;
}

void SyncManager::Clock::set(double pts, bool force) {
    mPts = pts;
    mLastUpdatedUs = getCurrentTimeUs();
    mTimeOffset = mPts - (mLastUpdatedUs / 1e6);
}

double SyncManager::Clock::get() const {
    if (mPaused) return mPts;
    int64_t now = getCurrentTimeUs();
    return mTimeOffset + ((now - mLastUpdatedUs) / 1e6) * mSpeed;
}

void SyncManager::Clock::update(double pts) {
    set(pts, true);
}

void SyncManager::Clock::setSpeed(double speed) {
    update(get());
    mSpeed = speed;
}

int64_t SyncManager::Clock::getCurrentTimeUs() {
    int64_t time = av_gettime_relative();
    if (time < 0) {
        std::cerr << "av_gettime_relative 失败，返回 0\n";
        return 0;
    }
    return time;
}
} // namespace yffplayer
