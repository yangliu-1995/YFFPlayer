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
    std::shared_ptr<PacketQueue> packetQueue_;
    std::shared_ptr<FrameQueue<FrameHandle>> frameQueue_;
    AVCodecContext* codecCtx_{nullptr};
    AVRational timeBase_;
    std::atomic<bool> paused_{false};
    std::mutex mutex_;
    std::condition_variable cond_;

    void decodeLoop() override;
};

}  // namespace yffplayer
