#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "BufferQueue.h"
#include "DemuxerCallback.h"
#include "Logger.h"
#include "MediaInfo.h"
#include "PlayerTypes.h"

extern "C" {
#include <libavcodec/packet.h>
}

namespace yffplayer {
class Demuxer {
   public:
    explicit Demuxer(std::shared_ptr<BufferQueue<AVPacket*>> audioBuffer,
                     std::shared_ptr<BufferQueue<AVPacket*>> videoBuffer,
                     std::shared_ptr<Logger> logger,
                     std::shared_ptr<DemuxerCallback> callback = nullptr);
    ~Demuxer();

    // Open media file or URL
    bool open(const std::string& url);

    void start();

    void stop();

    void seek(int64_t position);

    void setPlaybackRate(float rate);

    bool isLive() const;

    MediaInfo getMediaInfo() const;

    // Set callback
    void setCallback(std::shared_ptr<DemuxerCallback> callback);

   private:
    std::shared_ptr<BufferQueue<AVPacket*>> mAudioBuffer;
    std::shared_ptr<BufferQueue<AVPacket*>> mVideoBuffer;
    std::shared_ptr<Logger> mLogger;
    std::shared_ptr<DemuxerCallback> mCallback;

    void readLoop();
    void updateState(DemuxerState state);
    void notifyError(ErrorCode code, const std::string& message);

    std::atomic<DemuxerState> mState{DemuxerState::IDLE};
    std::atomic<bool> mIsRunning{false};
    std::atomic<bool> mIsSeeking{false};
    std::atomic<int64_t> mSeekPosition{0};
    std::atomic<bool> mIsLive{false};
    std::atomic<float> mPlaybackRate{1.0f};
    std::mutex mMutex;
    std::string mUrl;
    std::thread mReadThread;
    MediaInfo mMediaInfo;
};
}  // namespace yffplayer
