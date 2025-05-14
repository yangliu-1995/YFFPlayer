#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "AudioDecoder.h"
#include "AudioRenderer.h"
#include "BufferQueue.h"
#include "Demuxer.h"
#include "DemuxerCallback.h"
#include "Logger.h"
#include "MediaInfo.h"
#include "PlayerCallback.h"
#include "PlayerTypes.h"
#include "VideoDecoder.h"
#include "VideoRenderer.h"

namespace yffplayer {

class Player : public RendererCallback, public DemuxerCallback {
   public:
    Player(std::shared_ptr<PlayerCallback> callback,
           std::shared_ptr<AudioRenderer> audioRenderer,
           std::shared_ptr<VideoRenderer> videoRenderer,
           std::shared_ptr<Logger> logger);
    ~Player();

    // 播放控制
    bool open(const std::string& url);
    bool start();
    bool pause();
    bool resume();
    bool stop();
    bool close();
    bool seek(int64_t position);

    // 获取播放状态
    PlayerState getState() const;
    int64_t getCurrentPosition() const;
    int64_t getDuration() const;

    // 设置音量
    void setVolume(float volume);
    float getVolume() const;

    // 设置播放速度
    void setPlaybackRate(float rate);
    float getPlaybackRate() const;

    // 设置静音
    void setMute(bool mute);
    bool isMuted() const;

    // AudioRenderCallback接口实现
    void onAudioFrameRendered(const AudioFrame& frame) override;

    // VideoRenderCallback接口实现
    void onVideoFrameRendered(const VideoFrame& frame) override;

    // DemuxerCallback接口实现
    // 通知解复用器状态变化
    void onDemuxerStateChanged(DemuxerState state) override;
    // 通知解复用器出错
    void onDemuxerError(const Error& error) override;
    // 通知媒体文件已到结尾
    void onEndOfFile() override;
    // 通知媒体信息已获取
    void onMediaInfoReady(const MediaInfo& info) override;
    // 通知跳转操作完成
    void onSeekCompleted(int64_t position) override;

   private:
    // 播放器状态
    std::atomic<PlayerState> mState{PlayerState::IDLE};

    // 回调接口
    std::shared_ptr<PlayerCallback> mCallback;

    // 渲染器
    std::shared_ptr<AudioRenderer> mAudioRenderer;
    std::shared_ptr<VideoRenderer> mVideoRenderer;

    // 日志
    std::shared_ptr<Logger> mLogger;

    // 媒体信息
    MediaInfo mMediaInfo;

    // 缓冲区
    std::shared_ptr<BufferQueue<AVPacket*>> mAudioPacketBuffer;
    std::shared_ptr<BufferQueue<AVPacket*>> mVideoPacketBuffer;
    std::shared_ptr<BufferQueue<std::shared_ptr<AudioFrame>>> mAudioFrameBuffer;
    std::shared_ptr<BufferQueue<std::shared_ptr<VideoFrame>>> mVideoFrameBuffer;

    // 解复用器和解码器
    std::shared_ptr<Demuxer> mDemuxer;
    std::shared_ptr<AudioDecoder> mAudioDecoder;
    std::shared_ptr<VideoDecoder> mVideoDecoder;

    // 播放线程
    std::thread mVideoPlayThread;
    std::atomic<bool> mIsPlaying{false};

    // 时钟同步
    std::atomic<int64_t> mAudioClock{0};  // 音频时钟，微秒
    std::atomic<int64_t> mVideoClock{0};  // 视频时钟，微秒
    std::atomic<int64_t> mStartTime{0};   // 开始播放时间，微秒

    // 播放控制
    std::atomic<float> mPlaybackRate{1.0f};
    std::mutex mStateMutex;

    // 视频播放线程函数
    void videoPlayLoop();

    // 更新播放器状态
    void updateState(PlayerState state);

    // 获取当前系统时间（微秒）
    int64_t getCurrentTimeUs();

    // 计算音视频同步延迟
    int64_t calculateSyncDelay(int64_t videoPts);

    // 播放下一帧音频
    bool playNextAudioFrame();

    // 清空packetBuffer
    void clearPacketBuffer();
};

}  // namespace yffplayer
