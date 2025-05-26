#pragma once

#include <thread>
#include <memory>
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "VideoFrame.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace yffplayer {

class VideoDecoder {
public:
    VideoDecoder(std::shared_ptr<PacketQueue> packetQueue,
                 std::shared_ptr<FrameQueue<VideoFrame>> frameQueue,
                 AVCodecParameters* codecParams,
                 AVRational timeBase);
    ~VideoDecoder();

    void start();
    void stop();

private:
    std::shared_ptr<PacketQueue> mPacketQueue;
    std::shared_ptr<FrameQueue<VideoFrame>> mFrameQueue;
    AVCodecContext* mCodecCtx = nullptr;
    SwsContext* mSwsCtx = nullptr;
    AVPixelFormat mTargetFormat = AV_PIX_FMT_RGB24;
    AVRational mTimeBase;
    bool mStopped = false;
    std::thread mDecodeThread;

    void decodeLoop();
    int64_t toMs(int64_t pts, AVRational timeBase);
};

} // namespace yffplayer
