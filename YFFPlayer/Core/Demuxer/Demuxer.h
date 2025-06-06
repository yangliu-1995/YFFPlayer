#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "DemuxerCallback.h"
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
    void setCallback(std::shared_ptr<DemuxerCallback> callback);

private:
    void demuxLoop();

    std::shared_ptr<PacketQueue> audioQueue_;
    std::shared_ptr<PacketQueue> videoQueue_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> seeking_{false};
    std::atomic<bool> stopRequested_{false};

    std::mutex mutex_;
    std::condition_variable cond_;

    AVFormatContext* formatCtx_ = nullptr;
    int audioStreamIndex_ = -1;
    int videoStreamIndex_ = -1;

    std::weak_ptr<DemuxerCallback> callback_;
};

}  // namespace yffplayer
