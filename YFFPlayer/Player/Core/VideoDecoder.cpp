#include "VideoDecoder.h"

#include <chrono>
#include <thread>

// 假设使用FFmpeg库
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

namespace yffplayer {

VideoDecoder::VideoDecoder(
    std::shared_ptr<BufferQueue<AVPacket*>> packetBuffer,
    std::shared_ptr<BufferQueue<std::shared_ptr<VideoFrame>>> frameBuffer,
    std::shared_ptr<Logger> logger)
    : mPacketBuffer(packetBuffer), mFrameBuffer(frameBuffer) {
    mLogger = logger;
    mType = DecoderType::VIDEO;
}

VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::open(AVCodecParameters* codecParam) {
    // 查找解码器
    const AVCodec* decoder = avcodec_find_decoder(codecParam->codec_id);
    if (!decoder) {
        mLogger->log(LogLevel::Error, "AudioDecoder",
                     "找不到解码器: " + std::to_string(codecParam->codec_id));
        return false;
    }

    // 创建解码上下文
    mCodecContext = avcodec_alloc_context3(decoder);
    if (!mCodecContext) {
        mLogger->log(LogLevel::Error, "VideoDecoder", "无法创建解码上下文");
        return false;
    }

    if (avcodec_parameters_to_context((AVCodecContext*)mCodecContext,
                                      codecParam) < 0) {
        mLogger->log(LogLevel::Error, "VideoDecoder", "无法设置解码器参数");
        avcodec_free_context((AVCodecContext**)&mCodecContext);
        return false;
    }
    ((AVCodecContext*)mCodecContext)->thread_count = 4;  // 设置线程数
    // 打开解码器
    if (avcodec_open2((AVCodecContext*)mCodecContext, decoder, nullptr) < 0) {
        mLogger->log(LogLevel::Error, "VideoDecoder", "无法打开解码器");
        avcodec_free_context((AVCodecContext**)&mCodecContext);
        return false;
    }

    // 创建图像转换上下文
    mSwsContext = nullptr;  // 将在第一帧时初始化

    mLogger->log(LogLevel::Info, "VideoDecoder", "视频解码器初始化成功");
    return true;
}

void VideoDecoder::start() {
    if (mIsRunning) {
        return;
    }

    mIsRunning = true;
    mDecodeThread = std::thread(&VideoDecoder::decodeLoop, this);
    mLogger->log(LogLevel::Info, "VideoDecoder", "视频解码线程已启动");
}

void VideoDecoder::stop() {
    if (!mIsRunning) {
        return;
    }

    mIsRunning = false;
    if (mDecodeThread.joinable()) {
        mDecodeThread.join();
    }
    mLogger->log(LogLevel::Info, "VideoDecoder", "视频解码线程已停止");
}

void VideoDecoder::close() {
    stop();

    if (mSwsContext) {
        sws_freeContext((SwsContext*)mSwsContext);
        mSwsContext = nullptr;
    }

    if (mCodecContext) {
        avcodec_close((AVCodecContext*)mCodecContext);
        avcodec_free_context((AVCodecContext**)&mCodecContext);
        mCodecContext = nullptr;
    }

    mLogger->log(LogLevel::Info, "VideoDecoder", "视频解码器已关闭");
}

int64_t VideoDecoder::timestampToMicroseconds(int64_t timestamp,
                                              int timebase_num,
                                              int timebase_den) {
    // 使用 av_rescale_q 进行更安全的时间戳转换
    AVRational timebase = {timebase_num, timebase_den};
    AVRational microseconds = {1, 1000000};
    return av_rescale_q(timestamp, timebase, microseconds);
}

PixelFormat VideoDecoder::convertAVPixelFormat(int format) {
    AVPixelFormat inFormat = (AVPixelFormat)format;
    switch (inFormat) {
        case AV_PIX_FMT_YUV420P:
            return PixelFormat::YUV420P;
        case AV_PIX_FMT_RGB24:
            return PixelFormat::RGB24;
        case AV_PIX_FMT_NV12:
            return PixelFormat::NV12;
        default:
            return PixelFormat::RGB24;  // 默认转为RGB24
    }
}

bool VideoDecoder::convertFrame(AVFrame* srcFrame,
                                std::shared_ptr<VideoFrame> dstFrame) {
    AVCodecContext* ctx = (AVCodecContext*)mCodecContext;
    AVPixelFormat srcFormat = (AVPixelFormat)srcFrame->format;
    AVPixelFormat dstFormat;

    // 设置基本参数
    dstFrame->width = srcFrame->width;
    dstFrame->height = srcFrame->height;

    // 确定目标格式
    if (srcFormat == AV_PIX_FMT_YUV420P) {
        dstFormat = AV_PIX_FMT_YUV420P;
        dstFrame->format = PixelFormat::YUV420P;
    } else if (srcFormat == AV_PIX_FMT_RGB24) {
        dstFormat = AV_PIX_FMT_RGB24;
        dstFrame->format = PixelFormat::RGB24;
    } else if (srcFormat == AV_PIX_FMT_NV12) {
        dstFormat = AV_PIX_FMT_NV12;
        dstFrame->format = PixelFormat::NV12;
    } else {
        // 其他格式统一转为RGB24
        dstFormat = AV_PIX_FMT_RGB24;
        dstFrame->format = PixelFormat::RGB24;
    }

    // 如果源格式是我们支持的格式之一，可以直接复制数据而不需要转换
    if (srcFormat == dstFormat) {
        // 分配内存并复制数据
        if (dstFormat == AV_PIX_FMT_RGB24) {
            int bufferSize = srcFrame->width * srcFrame->height * 3;
            dstFrame->data[0] = (uint8_t*)av_malloc(bufferSize);
            dstFrame->linesize[0] = srcFrame->linesize[0];
            memcpy(dstFrame->data[0], srcFrame->data[0], bufferSize);
            dstFrame->data[1] = nullptr;
            dstFrame->data[2] = nullptr;
            dstFrame->linesize[1] = 0;
            dstFrame->linesize[2] = 0;
        } else if (dstFormat == AV_PIX_FMT_YUV420P) {
            // Y平面
            int ySize = srcFrame->width * srcFrame->height;
            dstFrame->data[0] = (uint8_t*)av_malloc(ySize);
            dstFrame->linesize[0] = srcFrame->linesize[0];
            memcpy(dstFrame->data[0], srcFrame->data[0], ySize);

            // U平面
            int uvSize = srcFrame->width * srcFrame->height / 4;
            dstFrame->data[1] = (uint8_t*)av_malloc(uvSize);
            dstFrame->linesize[1] = srcFrame->linesize[1];
            memcpy(dstFrame->data[1], srcFrame->data[1], uvSize);

            // V平面
            dstFrame->data[2] = (uint8_t*)av_malloc(uvSize);
            dstFrame->linesize[2] = srcFrame->linesize[2];
            memcpy(dstFrame->data[2], srcFrame->data[2], uvSize);
        } else if (dstFormat == AV_PIX_FMT_NV12) {
            // Y平面
            int ySize = srcFrame->width * srcFrame->height;
            dstFrame->data[0] = (uint8_t*)av_malloc(ySize);
            dstFrame->linesize[0] = srcFrame->linesize[0];
            memcpy(dstFrame->data[0], srcFrame->data[0], ySize);

            // UV平面
            int uvSize = srcFrame->width * srcFrame->height / 2;
            dstFrame->data[1] = (uint8_t*)av_malloc(uvSize);
            dstFrame->linesize[1] = srcFrame->linesize[1];
            memcpy(dstFrame->data[1], srcFrame->data[1], uvSize);

            dstFrame->data[2] = nullptr;
            dstFrame->linesize[2] = 0;
        }

        return true;
    }

    // 需要进行格式转换
    // 初始化或更新SwsContext
    if (!mSwsContext || mLastSrcFormat != srcFormat ||
        mLastDstFormat != dstFormat || mLastWidth != srcFrame->width ||
        mLastHeight != srcFrame->height) {
        if (mSwsContext) {
            sws_freeContext((SwsContext*)mSwsContext);
        }

        mSwsContext =
            sws_getContext(srcFrame->width, srcFrame->height, srcFormat,
                           srcFrame->width, srcFrame->height, dstFormat,
                           SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!mSwsContext) {
            mLogger->log(LogLevel::Error, "VideoDecoder",
                         "无法创建图像转换上下文");
            return false;
        }

        mLastSrcFormat = srcFormat;
        mLastDstFormat = dstFormat;
        mLastWidth = srcFrame->width;
        mLastHeight = srcFrame->height;
    }

    // 分配目标帧内存
    if (dstFormat == AV_PIX_FMT_RGB24) {
        int bufferSize =
            srcFrame->width * srcFrame->height * 3;  // RGB24每像素3字节
        dstFrame->data[0] = (uint8_t*)av_malloc(bufferSize);
        dstFrame->linesize[0] = srcFrame->width * 3;
        dstFrame->data[1] = nullptr;
        dstFrame->data[2] = nullptr;
        dstFrame->linesize[1] = 0;
        dstFrame->linesize[2] = 0;
    } else if (dstFormat == AV_PIX_FMT_YUV420P) {
        // Y平面
        dstFrame->data[0] =
            (uint8_t*)av_malloc(srcFrame->width * srcFrame->height);
        dstFrame->linesize[0] = srcFrame->width;
        // U平面
        dstFrame->data[1] =
            (uint8_t*)av_malloc(srcFrame->width * srcFrame->height / 4);
        dstFrame->linesize[1] = srcFrame->width / 2;
        // V平面
        dstFrame->data[2] =
            (uint8_t*)av_malloc(srcFrame->width * srcFrame->height / 4);
        dstFrame->linesize[2] = srcFrame->width / 2;
    } else if (dstFormat == AV_PIX_FMT_NV12) {
        // Y平面
        dstFrame->data[0] =
            (uint8_t*)av_malloc(srcFrame->width * srcFrame->height);
        dstFrame->linesize[0] = srcFrame->width;
        // UV平面
        dstFrame->data[1] =
            (uint8_t*)av_malloc(srcFrame->width * srcFrame->height / 2);
        dstFrame->linesize[1] = srcFrame->width;
        dstFrame->data[2] = nullptr;
        dstFrame->linesize[2] = 0;
    }

    // 执行图像转换
    int ret =
        sws_scale((SwsContext*)mSwsContext,
                  (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0,
                  srcFrame->height, dstFrame->data, dstFrame->linesize);

    if (ret <= 0) {
        mLogger->log(LogLevel::Error, "VideoDecoder", "图像转换失败");
        // 释放已分配的内存
        for (int i = 0; i < 3; i++) {
            if (dstFrame->data[i]) {
                av_free(dstFrame->data[i]);
                dstFrame->data[i] = nullptr;
            }
        }
        return false;
    }

    return true;
}

void VideoDecoder::decodeLoop() {
    AVCodecContext* ctx = (AVCodecContext*)mCodecContext;
    AVPacket* avPacket = nullptr;
    AVFrame* avFrame = av_frame_alloc();

    while (mIsRunning) {
        try {
            // 首先检查输出帧缓冲区是否已满
            if (mFrameBuffer->full()) {
                // 如果缓冲区已满，睡眠一段时间后继续
                av_usleep(10000);  // 10毫秒 = 10000微秒
                continue;
            }

            // 尝试从缓冲区获取数据包
            if (!mPacketBuffer->tryPop(avPacket) || !avPacket) {
                // 如果缓冲区为空，睡眠一段时间后继续
                av_usleep(10000);  // 10毫秒 = 10000微秒
                continue;
            }

            // 发送数据包到解码器
            int ret = avcodec_send_packet(ctx, avPacket);
            if (ret < 0) {
                mLogger->log(LogLevel::Error, "VideoDecoder",
                             "发送数据包到解码器失败");
                continue;
            }

            // 接收解码后的帧
            while (ret >= 0 && !mFrameBuffer->full()) {
                ret = avcodec_receive_frame(ctx, avFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    mLogger->log(LogLevel::Error, "VideoDecoder",
                                 "从解码器接收帧失败");
                    break;
                }

                // 创建视频帧
                std::shared_ptr<VideoFrame> videoFrame =
                    std::make_shared<VideoFrame>();

                // 转换时间戳为微秒
                videoFrame->pts = timestampToMicroseconds(
                    avFrame->pts, ctx->time_base.num, ctx->time_base.den);

                // 计算持续时间（微秒）
                // 如果有帧率信息，使用帧率计算持续时间
                if (avFrame->sample_aspect_ratio.num > 0 &&
                    avFrame->sample_aspect_ratio.den > 0) {
                    videoFrame->duration = 1000000 *
                                           avFrame->sample_aspect_ratio.den /
                                           avFrame->sample_aspect_ratio.num;
                } else if (ctx->framerate.num > 0 && ctx->framerate.den > 0) {
                    videoFrame->duration =
                        1000000 * ctx->framerate.den / ctx->framerate.num;
                } else {
                    // 默认使用25fps
                    videoFrame->duration = 40000;  // 40ms = 25fps
                }

                // 转换帧格式（如果需要）
                if (!convertFrame(avFrame, videoFrame)) {
                    mLogger->log(LogLevel::Error, "VideoDecoder",
                                 "帧格式转换失败");
                    continue;
                }

                // 尝试将帧放入缓冲区
                if (!mFrameBuffer->tryPush(videoFrame)) {
                    // 如果推送失败（缓冲区已满），释放资源
                    for (int i = 0; i < 3; i++) {
                        if (videoFrame->data[i]) {
                            av_free(videoFrame->data[i]);
                        }
                    }
                    // 跳出循环，下一轮会先检查缓冲区是否已满
                    break;
                }
            }
        } catch (const std::exception& e) {
            mLogger->log(LogLevel::Error, "VideoDecoder",
                         std::string("解码循环异常: ") + e.what());
            av_usleep(10000);  // 10毫秒 = 10000微秒
        }
    }

    av_frame_free(&avFrame);
    av_packet_free(&avPacket);
}

}  // namespace yffplayer
