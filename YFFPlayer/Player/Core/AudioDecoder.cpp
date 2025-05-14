#include "AudioDecoder.h"

#include <chrono>
#include <thread>

// 假设使用FFmpeg库
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

namespace yffplayer {

AudioDecoder::AudioDecoder(
    std::shared_ptr<BufferQueue<AVPacket*>> packetBuffer,
    std::shared_ptr<BufferQueue<std::shared_ptr<AudioFrame>>> frameBuffer,
    std::shared_ptr<Logger> logger)
    : mPacketBuffer(packetBuffer), mFrameBuffer(frameBuffer) {
    mLogger = logger;
    mType = DecoderType::AUDIO;
}

AudioDecoder::~AudioDecoder() { close(); }

bool AudioDecoder::open(AVCodecParameters *codecParam) {
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
        mLogger->log(LogLevel::Error, "AudioDecoder", "无法创建解码上下文");
        return false;
    }

    if (avcodec_parameters_to_context((AVCodecContext*)mCodecContext, codecParam) < 0) {
        mLogger->log(LogLevel::Error, "AudioDecoder", "无法设置解码器参数");
        avcodec_free_context((AVCodecContext**)&mCodecContext);
        return false;
    }

    // 打开解码器
    if (avcodec_open2((AVCodecContext*)mCodecContext, decoder, nullptr) < 0) {
        mLogger->log(LogLevel::Error, "AudioDecoder", "无法打开解码器");
        avcodec_free_context((AVCodecContext**)&mCodecContext);
        return false;
    }

    // 创建重采样上下文
    mSwrContext = swr_alloc();
    if (!mSwrContext) {
        mLogger->log(LogLevel::Error, "AudioDecoder", "无法创建重采样上下文");
        avcodec_close((AVCodecContext*)mCodecContext);
        avcodec_free_context((AVCodecContext**)&mCodecContext);
        return false;
    }

    mLogger->log(LogLevel::Info, "AudioDecoder", "音频解码器初始化成功");
    return true;
}

void AudioDecoder::start() {
    if (mIsRunning) {
        return;
    }

    mIsRunning = true;
    mDecodeThread = std::thread(&AudioDecoder::decodeLoop, this);
    mLogger->log(LogLevel::Info, "AudioDecoder", "音频解码线程已启动");
}

void AudioDecoder::stop() {
    if (!mIsRunning) {
        return;
    }

    mIsRunning = false;
    if (mDecodeThread.joinable()) {
        mDecodeThread.join();
    }
    mLogger->log(LogLevel::Info, "AudioDecoder", "音频解码线程已停止");
}

void AudioDecoder::close() {
    stop();

    if (mSwrContext) {
        swr_free((SwrContext**)&mSwrContext);
        mSwrContext = nullptr;
    }

    if (mCodecContext) {
        avcodec_close((AVCodecContext*)mCodecContext);
        avcodec_free_context((AVCodecContext**)&mCodecContext);
        mCodecContext = nullptr;
    }

    mLogger->log(LogLevel::Info, "AudioDecoder", "音频解码器已关闭");
}

int64_t AudioDecoder::timestampToMicroseconds(int64_t timestamp,
                                              int timebase_num,
                                              int timebase_den) {
    // 使用 av_rescale_q 进行更安全的时间戳转换
    AVRational timebase = {timebase_num, timebase_den};
    AVRational microseconds = {1, 1000000};
    return av_rescale_q(timestamp, timebase, microseconds);
}

bool AudioDecoder::resampleAudio(void* srcData, int srcSamples,
                                 int srcSampleRate, int srcChannels,
                                 void* dstData, int64_t& dstSamples) {
    AVCodecContext* ctx = (AVCodecContext*)mCodecContext;
    SwrContext* swr = (SwrContext*)mSwrContext;

    // 配置重采样上下文
    av_opt_set_int(swr, "in_channel_count", srcChannels, 0);
    av_opt_set_int(swr, "out_channel_count", kAudioTargetChannels, 0);
    av_opt_set_int(swr, "in_sample_rate", srcSampleRate, 0);
    av_opt_set_int(swr, "out_sample_rate", kAudioTargetSampleRate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    // 初始化重采样上下文
    if (swr_init(swr) < 0) {
        mLogger->log(LogLevel::Error, "AudioDecoder", "重采样上下文初始化失败");
        return false;
    }

    // 计算目标采样数
    dstSamples = av_rescale_rnd(srcSamples, kAudioTargetSampleRate, srcSampleRate,
                                AV_ROUND_UP);

    // 执行重采样
    int ret = swr_convert(swr, (uint8_t**)&dstData, dstSamples,
                          (const uint8_t**)&srcData, srcSamples);
    if (ret < 0) {
        mLogger->log(LogLevel::Error, "AudioDecoder", "音频重采样失败");
        return false;
    }

    dstSamples = ret;
    return true;
}

void AudioDecoder::decodeLoop() {
    AVCodecContext* ctx = (AVCodecContext*)mCodecContext;
    AVPacket* avPacket = nullptr;
    AVFrame* avFrame = av_frame_alloc();

    while (mIsRunning) {
        try {
            // 首先检查输出帧缓冲区是否已满
            if (mFrameBuffer->full()) {
                // 如果缓冲区已满，睡眠一段时间后继续
//                mLogger->log(LogLevel::Warning, "AudioDecoder",
//                                "缓冲区已满，等待数据处理");
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
                mLogger->log(LogLevel::Error, "AudioDecoder",
                             "发送数据包到解码器失败");
                continue;
            }

            // 接收解码后的帧
            while (ret >= 0 && !mFrameBuffer->full()) {
                ret = avcodec_receive_frame(ctx, avFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    mLogger->log(LogLevel::Error, "AudioDecoder",
                                 "从解码器接收帧失败");
                    break;
                }

                // 创建音频帧
                std::shared_ptr<AudioFrame> audioFrame =
                    std::make_shared<AudioFrame>();

                // 转换时间戳为微秒
                audioFrame->pts = timestampToMicroseconds(
                    avFrame->pts, ctx->time_base.num, ctx->time_base.den);

                // 计算持续时间（微秒）
                audioFrame->duration =
                    1000000 * avFrame->nb_samples / avFrame->sample_rate;

                // 设置原始音频参数
                audioFrame->channels = avFrame->channels;
                audioFrame->sampleRate = avFrame->sample_rate;
                audioFrame->bitDepth =
                    av_get_bytes_per_sample(ctx->sample_fmt) * 8;

                // 分配重采样后的数据缓冲区
                int64_t dstSamples = 0;
                int dstBufferSize = av_samples_get_buffer_size(
                                                               nullptr, kAudioTargetChannels, avFrame->nb_samples * 2,
                                                               AV_SAMPLE_FMT_S16, 0);
                uint8_t* dstData = (uint8_t*)av_malloc(dstBufferSize);

                // 执行重采样
                if (resampleAudio(avFrame->data[0], avFrame->nb_samples,
                                  avFrame->sample_rate, avFrame->channels,
                                  dstData, dstSamples)) {
                    // 设置重采样后的参数
                    audioFrame->data = dstData;
                    audioFrame->size =
                    dstSamples * kAudioTargetChannels * (kAudioTargetBitDepth / 8);
                    audioFrame->channels = kAudioTargetChannels;
                    audioFrame->sampleRate = kAudioTargetSampleRate;
                    audioFrame->bitDepth = kAudioTargetBitDepth;

                    // 尝试将帧放入缓冲区
                    if (!mFrameBuffer->tryPush(audioFrame)) {
                        // 如果推送失败（缓冲区已满），释放资源
                        av_free(dstData);
                        // 跳出循环，下一轮会先检查缓冲区是否已满
                        break;
                    }
                } else {
                    av_free(dstData);
                }
            }
        } catch (const std::exception& e) {
            mLogger->log(LogLevel::Error, "AudioDecoder",
                         std::string("解码循环异常: ") + e.what());
            av_usleep(10000);  // 10毫秒 = 10000微秒
        }
    }

    av_frame_free(&avFrame);
    av_packet_free(&avPacket);
}

}  // namespace yffplayer
