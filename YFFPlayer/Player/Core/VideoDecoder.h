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

    // 图像转换上下文
    void* mSwsContext{nullptr};

    // 解码上下文
    void* mCodecContext{nullptr};

    // 上一次转换的参数，用于优化SwsContext的创建
    int mLastSrcFormat{-1};
    int mLastDstFormat{-1};
    int mLastWidth{0};
    int mLastHeight{0};

    // 转换时间戳为微秒
    int64_t timestampToMicroseconds(int64_t timestamp, int timebase_num,
                                    int timebase_den);

    // 转换AVPixelFormat到自定义PixelFormat
    PixelFormat convertAVPixelFormat(int format);

    // 转换帧格式
    bool convertFrame(AVFrame* srcFrame, std::shared_ptr<VideoFrame> dstFrame);

    void decodeLoop() override;
};

}  // namespace yffplayer
