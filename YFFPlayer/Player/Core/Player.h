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

    // Playback control
    bool open(const std::string& url);
    bool start();
    bool pause();
    bool resume();
    bool stop();
    bool close();
    bool seek(int64_t position);

    // Get playback status
    PlayerState getState() const;
    int64_t getCurrentPosition() const;
    int64_t getDuration() const;

    // Set volume
    void setVolume(float volume);
    float getVolume() const;

    // Set playback speed
    void setPlaybackRate(float rate);
    float getPlaybackRate() const;

    // Set mute
    void setMute(bool mute);
    bool isMuted() const;

    // AudioRenderCallback interface implementation
    void onAudioFrameRendered(const AudioFrame& frame) override;

    // VideoRenderCallback interface implementation
    void onVideoFrameRendered(const VideoFrame& frame) override;

    // DemuxerCallback interface implementation
    // Notify demuxer state changes
    void onDemuxerStateChanged(DemuxerState state) override;
    // Notify demuxer errors
    void onDemuxerError(const Error& error) override;
    // Notify when media file reaches the end
    void onEndOfFile() override;
    // Notify when media information is ready
    void onMediaInfoReady(const MediaInfo& info) override;
    // Notify when seek operation is completed
    void onSeekCompleted(int64_t position) override;

   private:
    // Player state
    std::atomic<PlayerState> mState{PlayerState::IDLE};

    // Callback interface
    std::shared_ptr<PlayerCallback> mCallback;

    // Renderers
    std::shared_ptr<AudioRenderer> mAudioRenderer;
    std::shared_ptr<VideoRenderer> mVideoRenderer;

    // Logger
    std::shared_ptr<Logger> mLogger;

    // Media information
    MediaInfo mMediaInfo;

    // Buffers
    std::shared_ptr<BufferQueue<AVPacket*>> mAudioPacketBuffer;
    std::shared_ptr<BufferQueue<AVPacket*>> mVideoPacketBuffer;
    std::shared_ptr<BufferQueue<std::shared_ptr<AudioFrame>>> mAudioFrameBuffer;
    std::shared_ptr<BufferQueue<std::shared_ptr<VideoFrame>>> mVideoFrameBuffer;

    // Demuxer and decoders
    std::shared_ptr<Demuxer> mDemuxer;
    std::shared_ptr<AudioDecoder> mAudioDecoder;
    std::shared_ptr<VideoDecoder> mVideoDecoder;

    // Playback thread
    std::thread mVideoPlayThread;
    std::atomic<bool> mIsPlaying{false};

    // Clock synchronization
    std::atomic<int64_t> mAudioClock{0};  // Audio clock, microseconds
    std::atomic<int64_t> mVideoClock{0};  // Video clock, microseconds
    std::atomic<int64_t> mStartTime{0};   // Playback start time, microseconds

    // Playback control
    std::atomic<float> mPlaybackRate{1.0f};
    std::mutex mStateMutex;

    // Video playback thread function
    void videoPlayLoop();

    // Update player state
    void updateState(PlayerState state);

    // Get current system time (microseconds)
    int64_t getCurrentTimeUs();

    // Calculate audio-video sync delay
    int64_t calculateSyncDelay(int64_t videoPts);

    // Play next audio frame
    bool playNextAudioFrame();

    // Clear packet buffer
    void clearPacketBuffer();
};

}  // namespace yffplayer
