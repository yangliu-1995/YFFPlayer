// AudioDecoder.cpp
#include "AudioDecoder.h"
#include <iostream>
#include <vector>

#include "Packet.h"

namespace yffplayer {

AudioDecoder::AudioDecoder(std::shared_ptr<PacketQueue> packetQueue,
                           std::shared_ptr<FrameQueue<AudioFrame>> frameQueue)
    : mPacketQueue(std::move(packetQueue)), mFrameQueue(std::move(frameQueue)) {
}

AudioDecoder::~AudioDecoder() {
    stop();
    if (mSwrCtx) swr_free(&mSwrCtx);
    if (mCodecCtx) avcodec_free_context(&mCodecCtx);
}

bool AudioDecoder::open(AVCodecParameters* codecParams, AVRational timeBase) {
    mTimeBase = timeBase;
    
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        std::cerr << "Audio codec not found" << std::endl;
        return false;
    }

    mCodecCtx = avcodec_alloc_context3(codec);
    if (!mCodecCtx) {
        std::cerr << "Failed to allocate audio codec context" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(mCodecCtx, codecParams) < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return false;
    }

    if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
        std::cerr << "Failed to open audio codec" << std::endl;
        return false;
    }

    // ✅ 目标布局（44100Hz, 2 channels, S16）
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, 2);

    // ✅ 确保输入布局是有效的，否则 fallback 到默认
    if (!av_channel_layout_check(&mCodecCtx->ch_layout)) {
        av_channel_layout_default(&mCodecCtx->ch_layout, mCodecCtx->ch_layout.nb_channels);
    }

    mSwrCtx = swr_alloc();

    if (!mSwrCtx) {
        std::cerr << "Failed to allocate SwrContext" << std::endl;
        return false;
    }

    if (swr_alloc_set_opts2(&mSwrCtx,
                            &outLayout, AV_SAMPLE_FMT_S16, 44100,
                            &mCodecCtx->ch_layout, mCodecCtx->sample_fmt, mCodecCtx->sample_rate,
                            0, nullptr) < 0) {
        std::cerr << "swr_alloc_set_opts2 failed" << std::endl;
        return false;
    }

    if (swr_init(mSwrCtx) < 0) {
        std::cerr << "Failed to initialize SwrContext" << std::endl;
        return false;
    }

    // 注意 outLayout 是栈上临时变量，拷贝后记得释放
    av_channel_layout_uninit(&outLayout);
    
    return true;
}

void AudioDecoder::start() {
    mIsRunning = true;
    mPaused = false;
    mDecodeThread = std::thread(&AudioDecoder::decodeLoop, this);
    std::cout << "AudioDecoder started" << std::endl;
}

void AudioDecoder::stop() {
    mIsRunning = false;
    resume(); // 防止线程阻塞在暂停状态
    if (mDecodeThread.joinable()) {
        mDecodeThread.join();
    }
    if (mCodecCtx && avcodec_is_open(mCodecCtx)) {
        avcodec_close(mCodecCtx);
        avcodec_free_context(&mCodecCtx);
        mCodecCtx = nullptr;
    }
}

void AudioDecoder::pause() {
    mPaused = true;
}

void AudioDecoder::resume() {
    mPaused = false;
    mCond.notify_all();
}

void AudioDecoder::flush() {
    mFrameQueue->clear();
    if (mCodecCtx) {
        avcodec_flush_buffers(mCodecCtx);
    }
}

void AudioDecoder::decodeLoop() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame) {
        std::cerr << "Failed to allocate packet or frame" << std::endl;
        av_packet_free(&packet);
        av_frame_free(&frame);
        return;
    }

    while (mIsRunning) { // 使用mIsRunning替代!mStopped
        // 处理暂停逻辑
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, [this] {
            return !mPaused || mIsRunning; // 使用!mIsRunning替代mStopped
        });
        lock.unlock();
        
        if (!mIsRunning) { // 使用!mIsRunning替代mStopped
            break;
        }
        
        auto pkt = mPacketQueue->pop();
        if (!pkt) break; // 队列已关闭

        auto avPacket = pkt->mPacket;
        if (!avPacket) {
            av_packet_unref(pkt->mPacket); // 清理 Packet 内的 AVPacket
            continue;
        }

        int ret = avcodec_send_packet(mCodecCtx, avPacket);
        if (ret < 0) {
            std::cerr << "Failed to send packet: " << av_err2str(ret) << std::endl;
            av_packet_unref(avPacket);
            continue;
        }

        while (true) {
            ret = avcodec_receive_frame(mCodecCtx, frame);
            if (ret == 0) {
                // 计算重采样输出大小
                int outSamples = av_rescale_rnd(
                    swr_get_delay(mSwrCtx, mCodecCtx->sample_rate) + frame->nb_samples,
                    44100, mCodecCtx->sample_rate, AV_ROUND_UP);
                int outChannels = 2;
                int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                int outBufferSize = outSamples * outChannels * bytesPerSample;
                std::vector<uint8_t> buffer(outBufferSize);
                uint8_t* out[] = { buffer.data() };

                // 重采样
                int samples = swr_convert(mSwrCtx, out, outSamples,
                                        (const uint8_t**)frame->data, frame->nb_samples);
                if (samples < 0) {
                    std::cerr << "swr_convert failed: " << samples << std::endl;
                    continue;
                }

                // 检查时间戳
                int64_t pts = (frame->pts == AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                pts = static_cast<int64_t>(pts * av_q2d(mTimeBase) * 1000);
                int64_t dur = samples * 1000 / 44100;

                // 推送到 FrameQueue
                mFrameQueue->push(std::make_shared<AudioFrame>(
                    pts, dur, 44100, 2, samples, std::move(buffer)));
            } else if (ret == AVERROR(EAGAIN)) {
                break; // 需要更多输入
            } else if (ret == AVERROR_EOF) {
                break; // 解码结束
            } else {
                std::cerr << "Failed to receive frame: " << av_err2str(ret) << std::endl;
                break;
            }
        }

        av_packet_unref(avPacket); // 清理当前包
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
}

} // namespace yffplayer
