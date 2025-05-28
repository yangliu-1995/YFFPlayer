#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "PacketQueue.h"

extern "C" {
struct AVFormatContext;
}

namespace yffplayer {

struct MediaInfo;

class Demuxer {
public:
    Demuxer(std::shared_ptr<PacketQueue> audioQueue, std::shared_ptr<PacketQueue> videoQueue);
    ~Demuxer();

    bool open(const std::string& url, MediaInfo& mediaInfo);
    void start();
    void stop();
    void pause();
    void resume();
    bool seek(int64_t timestampMs);

private:
    void demuxLoop();

    std::shared_ptr<PacketQueue> mAudioQueue;
    std::shared_ptr<PacketQueue> mVideoQueue;

    std::thread mThread;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mPaused{false};
    std::atomic<bool> mSeeking{false};
    std::atomic<bool> mStopRequested{false};

    std::mutex mMutex;
    std::condition_variable mCond;

    // FFmpeg context forward declarations
    AVFormatContext* mFormatCtx = nullptr;
    int mAudioStreamIndex = -1;
    int mVideoStreamIndex = -1;
};

}  // namespace yffplayer
