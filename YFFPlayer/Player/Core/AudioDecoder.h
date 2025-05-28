#pragma once

#include <memory>

#include "AudioFrame.h"
#include "Decoder.h"
#include "FrameQueue.h"
#include "PacketQueue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

namespace yffplayer {

class AudioDecoder : public Decoder {
public:
    AudioDecoder(std::shared_ptr<PacketQueue> packetQueue,
                 std::shared_ptr<FrameQueue<AudioFrame>> frameQueue);
    ~AudioDecoder() override;

    bool open(AVCodecParameters* codecParams, AVRational timeBase) override;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void flush() override;

private:
    std::shared_ptr<PacketQueue> mPacketQueue;
    std::shared_ptr<FrameQueue<AudioFrame>> mFrameQueue;
    AVCodecContext* mCodecCtx = nullptr;
    SwrContext* mSwrCtx = nullptr;
    AVRational mTimeBase;
    std::atomic<bool> mPaused{false};
    std::mutex mMutex;
    std::condition_variable mCond;

    void decodeLoop() override;
};

}  // namespace yffplayer
