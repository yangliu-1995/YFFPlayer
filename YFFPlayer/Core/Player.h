#pragma once
#include <atomic>
#include <memory>
#include <thread>

#include "AudioDecoder.h"
#include "AudioOutput.h"
#include "Demuxer.h"
#include "DemuxerCallback.h"
#include "FrameQueue.h"
#include "MediaInfo.h"
#include "PacketQueue.h"
#include "PlayerCallback.h"
#include "SonicAudioProcessor.h"
#include "SyncManager.h"
#include "VideoDecoder.h"
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
    
    // 同步模式控制
    void setSyncMode(SyncMode mode);
    SyncMode getSyncMode() const;

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

    std::unique_ptr<SyncManager> mSyncManager;
    std::unique_ptr<Demuxer> mDemuxer;
    std::shared_ptr<PacketQueue> mAudioPacketQueue;
    std::shared_ptr<PacketQueue> mVideoPacketQueue;
    std::shared_ptr<FrameQueue<AudioFrame>> mAudioFrameQueue;
    std::shared_ptr<FrameQueue<VideoFrame>> mVideoFrameQueue;
    std::unique_ptr<AudioDecoder> mAudioDecoder;
    std::unique_ptr<VideoDecoder> mVideoDecoder;
    std::shared_ptr<AudioOutput> mAudioOutput;
    std::shared_ptr<VideoOutput> mVideoOutput;
    std::thread mAudioOutputThread;
    std::thread mVideoOutputThread;
    std::thread mStopThread;
    MediaInfo mMediaInfo;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mPaused{false};
    std::atomic<int> mDroppedVideoFramesCount{0};
    std::atomic<float> mPlaybackRate{1.0f};  // 播放倍率
    std::shared_ptr<PlayerCallback> mCallback;
    std::unique_ptr<SonicAudioProcessor> mAudioProcessor;
};
}  // namespace yffplayer
