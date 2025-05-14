#pragma once

#include "BufferQueue.h"
#include "Decoder.h"
#include "Logger.h"
#include "VideoFrame.h"

extern "C" {
struct AVFrame;
struct AVPacket;
}

namespace yffplayer {
class VideoDecoder : public Decoder {
   public:
    VideoDecoder(
        std::shared_ptr<BufferQueue<AVPacket*>> packetBuffer,
        std::shared_ptr<BufferQueue<std::shared_ptr<VideoFrame>>> frameBuffer,
        std::shared_ptr<Logger> logger);
    ~VideoDecoder() override;

    bool open(AVCodecParameters* codecParam) override;
    void start() override;
    void stop() override;
    void close() override;

   private:
    std::shared_ptr<BufferQueue<AVPacket*>> mPacketBuffer;
    std::shared_ptr<BufferQueue<std::shared_ptr<VideoFrame>>> mFrameBuffer;

    // Image conversion context
    void* mSwsContext{nullptr};

    // Decoding context
    void* mCodecContext{nullptr};

    // Parameters from last conversion, used to optimize SwsContext creation
    int mLastSrcFormat{-1};
    int mLastDstFormat{-1};
    int mLastWidth{0};
    int mLastHeight{0};

    // Convert timestamp to microseconds
    int64_t timestampToMicroseconds(int64_t timestamp, int timebase_num,
                                    int timebase_den);

    // Convert AVPixelFormat to custom PixelFormat
    PixelFormat convertAVPixelFormat(int format);

    // Convert frame format
    bool convertFrame(AVFrame* srcFrame, std::shared_ptr<VideoFrame> dstFrame);

    void decodeLoop() override;
};

}  // namespace yffplayer
