#pragma once

#include <atomic>
#include <chrono>
#include <mutex>

namespace yffplayer {

enum class SyncMode {
    AUDIO,
    EXTERNAL_CLOCK
};

struct Clock {
    double mPts;           // 当前时间戳（秒）
    double mPtsDrift;      // 时间戳与实际时间的漂移（秒）
    double mLastUpdated;   // 上次更新时间（秒）
    double mSpeed;         // 播放速度
    std::atomic<bool> mPaused; // 暂停状态
};

class SyncManager {
public:
    SyncManager();
    ~SyncManager() = default;

    // 播放速度控制
    void setSpeed(float speed);
    float getSpeed() const;

    // 时钟更新
    void updateClock(int64_t pts, int64_t duration);

    // 延迟计算
    int64_t calculateDelay(int64_t pts, bool& shouldDropFrame);

    // 获取当前时钟
    int64_t getClock() const;

    // 同步模式控制
    void setSyncMode(SyncMode mode);
    SyncMode getSyncMode() const;

    // 外部时钟控制
    void startExternalClock();
    void pauseExternalClock();
    void resumeExternalClock();
    void seekExternalClock(int64_t positionMs);
    int64_t getExternalClock() const;
    void updateDriftWithPts(int64_t pts);

private:
    // 获取当前系统时间（秒）
    double getCurrentExternalTime() const;

    Clock mClock;                     // 时钟结构
    std::atomic<SyncMode> mSyncMode;  // 同步模式
    std::chrono::steady_clock::time_point mExternalClockBaseTime; // 外部时钟基准时间
    mutable std::mutex mClockMutex;   // 保护 Clock 结构的互斥锁
};
}  // namespace yffplayer
