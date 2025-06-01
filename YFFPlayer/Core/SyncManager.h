#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

namespace yffplayer {

// 同步模式枚举
enum class SyncMode {
    AUDIO_MASTER,    // 音频主时钟（默认）
    VIDEO_MASTER,    // 视频主时钟
    EXTERNAL_CLOCK   // 外部时钟
};

class SyncManager {
public:
    SyncManager() = default;
    ~SyncManager() = default;
    
    // 原有接口
    void setSpeed(float speed);
    float getSpeed() const;
    void updateClock(int64_t pts, int64_t duration);
    int64_t calculateDelay(int64_t pts, bool &shouldDropFrame);
    int64_t getClock() const;
    
    // 新增外部时钟接口
    void setSyncMode(SyncMode mode);
    SyncMode getSyncMode() const;
    
    // 外部时钟控制
    void startExternalClock();
    void pauseExternalClock();
    void resumeExternalClock();
    void seekExternalClock(int64_t positionMs);
    int64_t getExternalClock() const;
    
    // PTS漂移补偿
    void updateDriftWithPts(int64_t pts);
    
private:
    // 原有成员
    std::atomic<float> mSpeed{1.0};
    std::atomic<int64_t> mClock{0};
    
    // 新增成员
    std::atomic<SyncMode> mSyncMode{SyncMode::AUDIO_MASTER};
    
    // 外部时钟相关
    std::atomic<bool> mExternalClockPaused{false};
    std::atomic<int64_t> mExternalClockStartTime{0};  // 外部时钟开始时间
    std::chrono::steady_clock::time_point mExternalClockBaseTime;  // 系统时间基准点
    std::atomic<int64_t> mExternalClockPauseOffset{0};  // 暂停时的偏移量
    std::atomic<int64_t> mPtsDrift{0};  // PTS漂移补偿
    
    // 内部辅助方法
    int64_t getCurrentExternalTime() const;
};
}  // namespace yffplayer
