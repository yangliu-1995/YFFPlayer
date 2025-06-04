#pragma once

#include <memory>

#include "Decoder.h"
#include "FrameQueue.h"
#include "PacketQueue.h"
#include "FrameHandle.h"

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
    std::shared_ptr<PacketQueue> mPacketQueue;
    std::shared_ptr<FrameQueue<FrameHandle>> mFrameQueue;
    AVCodecContext* mCodecCtx = nullptr;
    AVRational mTimeBase;
    std::atomic<bool> mPaused{false};
    std::mutex mMutex;
    std::condition_variable mCond;

    void decodeLoop() override;
};

}  // namespace yffplayer
