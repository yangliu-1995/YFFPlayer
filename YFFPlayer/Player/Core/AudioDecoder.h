#pragma once

#include <memory>
#include <thread>
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "AudioFrame.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

namespace yffplayer {

class AudioDecoder {
public:
    AudioDecoder(std::shared_ptr<PacketQueue> packetQueue,
                 std::shared_ptr<FrameQueue<AudioFrame>> frameQueue,
                 AVCodecParameters* codecParams,
                 AVRational timeBase);
    ~AudioDecoder();

    void start();
    void stop();

private:
    std::shared_ptr<PacketQueue> mPacketQueue;
    std::shared_ptr<FrameQueue<AudioFrame>> mFrameQueue;
    AVCodecContext* mCodecCtx = nullptr;
    SwrContext* mSwrCtx = nullptr;
    AVRational mTimeBase;
    bool mStopped = false;
    std::thread mDecodeThread;

    void decodeLoop();
};

} // namespace yffplayer
