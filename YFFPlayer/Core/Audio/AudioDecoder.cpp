// AudioDecoder.cpp
#include "AudioDecoder.h"

#include <iostream>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#endif

#include "Packet.h"

namespace yffplayer {

AudioDecoder::AudioDecoder(std::shared_ptr<PacketQueue> packetQueue,
                           std::shared_ptr<FrameQueue<FrameHandle>> frameQueue)
    : mPacketQueue(std::move(packetQueue)), mFrameQueue(std::move(frameQueue)) {}

AudioDecoder::~AudioDecoder() {
    stop();
    std::cerr << "~AudioDecoder" << std::endl;
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

    // 移除重采样逻辑，decoder只负责解码

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
    resume();  // 防止线程阻塞在暂停状态
    if (mDecodeThread.joinable()) {
        mDecodeThread.join();
    }
    if (mCodecCtx && avcodec_is_open(mCodecCtx)) {
        avcodec_free_context(&mCodecCtx);
        mCodecCtx = nullptr;
    }
}

void AudioDecoder::pause() { mPaused = true; }

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
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.decoder.audio");
#endif
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame) {
        std::cerr << "Failed to allocate packet or frame" << std::endl;
        av_packet_free(&packet);
        av_frame_free(&frame);
        return;
    }

    while (mIsRunning) {  // 使用mIsRunning替代!mStopped
        // 处理暂停逻辑
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, [this] {
            return !mPaused;  // 使用!mIsRunning替代mStopped
        });
        lock.unlock();

        if (!mIsRunning) {  // 使用!mIsRunning替代mStopped
            break;
        }

        auto pkt = mPacketQueue->pop();
        if (!pkt) {
            continue;
        }

        auto avPacket = pkt->mPacket;
        if (!avPacket) {
            av_packet_unref(pkt->mPacket);  // 清理 Packet 内的 AVPacket
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
                AVFrame* frameClone = av_frame_alloc();
                av_frame_ref(frameClone, frame);
                if (!frameClone) {
                    std::cerr << "Failed to allocate frame clone" << std::endl;
                    continue;
                }
                if (frameClone->pts != AV_NOPTS_VALUE) {
                    frameClone->pts = static_cast<int64_t>(frameClone->pts * av_q2d(mTimeBase) * 1000);
                } else if (frameClone->best_effort_timestamp != AV_NOPTS_VALUE) {
                    frameClone->pts = static_cast<int64_t>(frameClone->best_effort_timestamp * av_q2d(mTimeBase) * 1000);
                }
                mFrameQueue->push(std::make_shared<FrameHandle>(frameClone));
            } else if (ret == AVERROR(EAGAIN)) {
                break;  // 需要更多输入
            } else if (ret == AVERROR_EOF) {
                break;  // 解码结束
            } else {
                std::cerr << "Failed to receive frame: " << av_err2str(ret) << std::endl;
                break;
            }
        }

        av_packet_unref(avPacket);  // 清理当前包
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    std::cerr << "AudioDecoder thread ended" << std::endl;
}

}  // namespace yffplayer
