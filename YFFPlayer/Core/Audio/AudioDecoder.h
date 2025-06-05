#pragma once

#include <memory>

#include "AudioFrame.h"
#include "Decoder.h"
#include "FrameHandle.h"
#include "FrameQueue.h"
#include "PacketQueue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

namespace yffplayer {

class AudioDecoder : public Decoder {
public:
    AudioDecoder(std::shared_ptr<PacketQueue> packetQueue,
                 std::shared_ptr<FrameQueue<FrameHandle>> frameQueue);
    ~AudioDecoder() override;

    bool open(AVCodecParameters* codecParams, AVRational timeBase) override;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void flush() override;
    AVSampleFormat getFormat() const;
    int getSampleRate() const;
    int getNbChannels() const;

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
