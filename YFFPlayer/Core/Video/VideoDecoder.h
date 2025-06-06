#pragma once

#include <memory>

#include "Decoder.h"
#include "FrameHandle.h"
#include "FrameQueue.h"
#include "PacketQueue.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace yffplayer {

class VideoDecoder : public Decoder {
public:
    VideoDecoder(std::shared_ptr<PacketQueue> packetQueue,
                 std::shared_ptr<FrameQueue<FrameHandle>> frameQueue);
    ~VideoDecoder() override;

    bool open(AVCodecParameters* codecParams, AVRational timeBase) override;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void flush() override;

private:
    std::shared_ptr<PacketQueue> packetQueue_;
    std::shared_ptr<FrameQueue<FrameHandle>> frameQueue_;
    AVCodecContext* codecCtx_ = nullptr;
    AVRational timeBase_;
    std::atomic<bool> paused_{false};
    std::mutex mutex_;
    std::condition_variable cond_;

    void decodeLoop() override;
};

}  // namespace yffplayer
