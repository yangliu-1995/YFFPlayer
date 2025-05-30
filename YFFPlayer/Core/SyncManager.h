#pragma once

#include <memory>
#include <mutex>

namespace yffplayer {
class SyncManager {
public:
    SyncManager() = default;
    ~SyncManager() = default;
    void setSpeed(float speed);
    float getSpeed() const;
    void updateClock(int64_t pts, int64_t duration);
    int64_t calculateDelay(int64_t pts, bool &shouldDropFrame);
    int64_t getClock() const;

private:
    std::atomic<float> mSpeed{1.0};
    std::atomic<int64_t> mClock{0};
};
}  // namespace yffplayer
