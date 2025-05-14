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
        std::shared_ptr<BufferQueue<std::shared_ptr<AudioFrame>>>
            frameBuffer,
        std::shared_ptr<Logger> logger);
    ~AudioDecoder() override;

    bool open(AVCodecParameters *codecParam) override;
    void start() override;
    void stop() override;
    void close() override;

   private:
    std::shared_ptr<BufferQueue<AVPacket*>> mPacketBuffer;
    std::shared_ptr<BufferQueue<std::shared_ptr<AudioFrame>>> mFrameBuffer;

    // 音频重采样上下文
    void* mSwrContext{nullptr};

    // 解码上下文
    void* mCodecContext{nullptr};

    // 转换时间戳为微秒
    int64_t timestampToMicroseconds(int64_t timestamp, int timebase_num,
                                    int timebase_den);

    // 重采样音频
    bool resampleAudio(void* srcData, int srcSamples, int srcSampleRate,
                       int srcChannels, void* dstData, int64_t& dstSamples);

    void decodeLoop() override;
};

}  // namespace yffplayer
