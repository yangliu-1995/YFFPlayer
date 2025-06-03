#pragma once

#include <mutex>
#include <vector>

// 前向声明 FFmpeg 函数
extern "C" int64_t av_gettime_relative();

namespace yffplayer {

class SyncManager {
public:
    SyncManager();

    // 暂停播放
    void pause();
    // 恢复播放
    void resume();
    // 跳转到指定时间戳
    void seek(double pts);
    // 设置播放速率
    void setPlaybackRate(double speed);
    // 获取当前同步时间
    double getSyncTime() const;
    // 计算延迟并决定是否丢帧
    double calcDelay(double pts, bool &shouldDrop);
    // 更新时间戳漂移
    void updatePtsDrift(double pts);

private:
    struct Clock {
        double mPts = 0.0;         // 当前时间戳
        double mTimeOffset = 0.0;  // 时间偏移（原 mPtsDrift）
        int64_t mLastUpdatedUs = 0;// 最后更新时间（微秒）
        double mSpeed = 1.0;       // 播放速率
        bool mPaused = false;      // 暂停状态

        // 设置时间戳
        void set(double pts, bool force = true);
        // 获取当前时间
        double get() const;
        // 更新时间戳
        void update(double pts);
        // 设置播放速率
        void setSpeed(double speed);
        // 获取当前时间（微秒）
        static int64_t getCurrentTimeUs();
    };

    Clock mClock;                 // 时钟对象
    mutable std::mutex mMutex;           // 互斥锁，确保线程安全
    static constexpr double MAX_DELAY = 0.1;     // 最大等待时间 100ms
    static constexpr double DROP_THRESHOLD = 0.05; // 丢帧阈值 50ms
};
}  // namespace yffplayer
