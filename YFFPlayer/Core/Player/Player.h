#pragma once
#include <atomic>
#include <memory>
#include <thread>

#include "AudioDecoder.h"
#include "AudioFrame.h"
#include "AudioFrameProcessor.h"
#include "AudioOutput.h"
#include "Demuxer.h"
#include "DemuxerCallback.h"
#include "FrameHandle.h"
#include "FrameQueue.h"
#include "MediaInfo.h"
#include "PacketQueue.h"
#include "PlayerCallback.h"
#include "SyncManager.h"
#include "VideoDecoder.h"
#include "VideoFrame.h"
#include "VideoFrameProcessor.h"
#include "VideoOutput.h"

namespace yffplayer {
class Player : public DemuxerCallback, public std::enable_shared_from_this<Player> {
public:
    Player(std::shared_ptr<AudioOutput> audioOutput, std::shared_ptr<VideoOutput> videoOutput,
           std::shared_ptr<PlayerCallback> callback);
    ~Player();

    bool open(const std::string& url, MediaInfo& mediaInfo);
    void start();
    void stop();
    void pause();
    void resume();
    bool seek(int64_t positionMs);
    void setPlaybackRate(float rate);  // 设置播放倍率
    float getPlaybackRate() const;     // 获取当前播放倍率

    // demuxer callback
    void onDemuxStarted() override;
    void onDemuxPaused() override;
    void onDemuxResumed() override;
    void onDemuxStopped() override;
    void onReadError(const Error& error) override;
    void onEndOfFile() override;
    void onNetworkError(const Error& error) override;
    void onSeekStarted(int64_t targetTimestampMs) override;
    void onSeekCompleted(int64_t actualTimestampMs) override;
    void onSeekFailed(int64_t targetTimestampMs, const Error& error) override;
    void onDemuxProgress(int64_t currentTimestampMs, int64_t durationMs) override;

private:
    void audioOutputThread();
    void videoOutputThread();
    void notifyProgressChanged();
    void syncClockIfNeeded(int64_t pts);

    std::unique_ptr<SyncManager> syncManager_;
    std::unique_ptr<Demuxer> demuxer_;
    std::shared_ptr<PacketQueue> audioPacketQueue_;
    std::shared_ptr<PacketQueue> videoPacketQueue_;
    std::shared_ptr<FrameQueue<FrameHandle>> audioFrameQueue_;
    std::shared_ptr<FrameQueue<FrameHandle>> videoFrameQueue_;
    std::unique_ptr<AudioDecoder> audioDecoder_;
    std::unique_ptr<VideoDecoder> videoDecoder_;
    std::shared_ptr<AudioOutput> audioOutput_;
    std::shared_ptr<VideoOutput> videoOutput_;
    std::thread audioOutputThread_;
    std::thread videoOutputThread_;
    std::thread stopThread_;
    MediaInfo mediaInfo_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> droppedVideoFramesCount_{0};
    std::atomic<float> playbackRate_{1.0f};      // 播放倍率
    std::atomic<bool> requiresSyncClock_{true};  // 标记是否需要漂移同步
    std::shared_ptr<PlayerCallback> callback_;
    std::unique_ptr<AudioFrameProcessor> audioProcessor_;
    std::unique_ptr<VideoFrameProcessor> videoProcessor_;

    double frameTimer_{0};
    double lastVideoPts_{0};  // 上一帧视频的PTS
    std::atomic<float> audioPlaybackRateDelt_{0.0f};
};
}  // namespace yffplayer
