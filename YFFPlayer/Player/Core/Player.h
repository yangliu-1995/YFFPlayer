#pragma once
#include <atomic>
#include <memory>
#include <thread>

#include "AudioDecoder.h"
#include "AudioOutput.h"
#include "Demuxer.h"
#include "FrameQueue.h"
#include "MediaInfo.h"
#include "PacketQueue.h"
#include "VideoDecoder.h"
#include "VideoOutput.h"

namespace yffplayer {
class Player {
public:
    Player(std::shared_ptr<AudioOutput> audioOutput, std::shared_ptr<VideoOutput> videoOutput);
    ~Player();

    bool open(const std::string& url, MediaInfo& mediaInfo);
    void start();
    void stop();
    void pause();
    void resume();
    bool seek(int64_t positionMs);

private:
    void audioOutputThread();
    void videoOutputThread();
    void stopThread();

    std::shared_ptr<Demuxer> mDemuxer;
    std::shared_ptr<PacketQueue> mAudioPacketQueue;
    std::shared_ptr<PacketQueue> mVideoPacketQueue;
    std::shared_ptr<FrameQueue<AudioFrame>> mAudioFrameQueue;
    std::shared_ptr<FrameQueue<VideoFrame>> mVideoFrameQueue;
    std::shared_ptr<AudioDecoder> mAudioDecoder;
    std::shared_ptr<VideoDecoder> mVideoDecoder;
    std::shared_ptr<AudioOutput> mAudioOutput;
    std::shared_ptr<VideoOutput> mVideoOutput;
    std::thread mAudioOutputThread;
    std::thread mVideoOutputThread;
    std::thread mStopThread;
    std::atomic<int64_t> mAudioClock{0};
    std::atomic<int64_t> mVideoClock{0};
    MediaInfo mMediaInfo;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mPaused{false};
    std::atomic<int> mDroppedVideoFramesCount{0};
};
}  // namespace yffplayer
