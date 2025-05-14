#pragma once

#include "AudioFrame.h"
#include "BufferQueue.h"
#include "Decoder.h"
#include "Logger.h"

extern "C" {
struct AVPacket;
}

namespace yffplayer {
class AudioDecoder : public Decoder {
   public:
    AudioDecoder(
        std::shared_ptr<BufferQueue<AVPacket*>> packetBuffer,
        std::shared_ptr<BufferQueue<std::shared_ptr<AudioFrame>>> frameBuffer,
        std::shared_ptr<Logger> logger);
    ~AudioDecoder() override;

    bool open(AVCodecParameters* codecParam) override;
    void start() override;
    void stop() override;
    void close() override;

   private:
    std::shared_ptr<BufferQueue<AVPacket*>> mPacketBuffer;
    std::shared_ptr<BufferQueue<std::shared_ptr<AudioFrame>>> mFrameBuffer;

    // Audio resampling context
    void* mSwrContext{nullptr};

    // Decoding context
    void* mCodecContext{nullptr};

    // Convert timestamp to microseconds
    int64_t timestampToMicroseconds(int64_t timestamp, int timebase_num,
                                    int timebase_den);

    // Resample audio
    bool resampleAudio(void* srcData, int srcSamples, int srcSampleRate,
                       int srcChannels, void* dstData, int64_t& dstSamples);

    void decodeLoop() override;
};

}  // namespace yffplayer
