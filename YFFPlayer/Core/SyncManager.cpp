#include "SyncManager.h"

namespace {
constexpr int kMaxDropDiff = 50;
}

namespace yffplayer {
void SyncManager::setSpeed(float speed) { mSpeed = speed; }

float SyncManager::getSpeed() const { return mSpeed; }

void SyncManager::updateClock(int64_t pts, int64_t duration) {
    mClock = pts + static_cast<int64_t>(duration / mSpeed.load());
}

int64_t SyncManager::calculateDelay(int64_t pts, bool& shouldDropFrame) {
    int64_t diff = pts - mClock;
    int64_t adjustedDiff = static_cast<int64_t>(diff / mSpeed.load());
    {
        std::lock_guard<std::mutex> lock(mMutex);
        shouldDropFrame = diff < -kMaxDropDiff;
    }
    return adjustedDiff >= 0 ? adjustedDiff : 0;
}

int64_t SyncManager::getClock() const {
    return mClock.load();
}

}  // namespace yffplayer
