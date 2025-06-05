#include "VideoDecoder.h"

#include <iostream>

#if defined(__APPLE__)
#include <pthread.h>
#endif

#include "Log.h"
#include "Packet.h"

namespace yffplayer {

static enum AVPixelFormat CodecContextGetFormat(struct AVCodecContext* s,
                                                const enum AVPixelFormat* fmt) {
    for (int i = 0; fmt[i] != AV_PIX_FMT_NONE; i++) {
        if (fmt[i] == AV_PIX_FMT_VIDEOTOOLBOX) {
            AVBufferRef* device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VIDEOTOOLBOX);
            if (!device_ctx) {
                break;
            }
            s->hw_device_ctx = device_ctx;
            return fmt[i];
        }
    }
    return fmt[0];
}

VideoDecoder::VideoDecoder(std::shared_ptr<PacketQueue> packetQueue,
                           std::shared_ptr<FrameQueue<FrameHandle>> frameQueue)
    : mPacketQueue(std::move(packetQueue)), mFrameQueue(std::move(frameQueue)) {}

VideoDecoder::~VideoDecoder() {
    stop();
    LogInfo << "~VideoDecoder" << std::endl;
}

bool VideoDecoder::open(AVCodecParameters* codecParams, AVRational timeBase) {
    mTimeBase = timeBase;

    mCodecCtx = avcodec_alloc_context3(nullptr);
    if (!mCodecCtx) {
        LogInfo << "Failed to allocate video codec context" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(mCodecCtx, codecParams) < 0) {
        LogInfo << "Failed to copy codec parameters" << std::endl;
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        LogInfo << "Video codec not found" << std::endl;
        return false;
    }
    if (codecParams->codec_id == AV_CODEC_ID_H264 || codecParams->codec_id == AV_CODEC_ID_HEVC) {
        mCodecCtx->get_format = CodecContextGetFormat;
        mCodecCtx->codec_id = codec->id;
    }

    if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
        LogInfo << "Failed to open codec" << std::endl;
        return false;
    }

    return true;
}

void VideoDecoder::start() {
    mIsRunning = true;
    mPaused = false;
    mDecodeThread = std::thread(&VideoDecoder::decodeLoop, this);
    LogInfo << "VideoDecoder started" << std::endl;
}

void VideoDecoder::stop() {
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

void VideoDecoder::pause() { mPaused = true; }

void VideoDecoder::resume() {
    mPaused = false;
    mCond.notify_all();
}

void VideoDecoder::flush() {
    mFrameQueue->clear();
    if (mCodecCtx) {
        avcodec_flush_buffers(mCodecCtx);
    }
}

void VideoDecoder::decodeLoop() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.decoder.video");
#endif
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    while (mIsRunning) {
        // 处理暂停逻辑
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, [this] { return !mPaused; });
        lock.unlock();

        if (!mIsRunning) {
            break;
        }

        auto pkt = mPacketQueue->pop();
        if (!pkt) {
            continue;
        }

        auto avPacket = pkt->mPacket;
        if (!avPacket) {
            LogInfo << "Received empty packet, skipping\n";
            continue;
        }

        int ret = avcodec_send_packet(mCodecCtx, avPacket);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                av_log(NULL, AV_LOG_WARNING,
                       "Decoder is full, need to receive frames before sending more packets.\n");
                continue;
            } else if (ret == AVERROR_EOF) {
                av_log(NULL, AV_LOG_INFO, "Decoder has been fully flushed.\n");
                break;  // 或 return，根据你的状态判断
            } else {
                av_log(NULL, AV_LOG_ERROR, "Failed to send packet to decoder: %s\n",
                       av_err2str(ret));
                continue;
            }
        }

        while (true) {
            av_frame_unref(frame);
            int ret = avcodec_receive_frame(mCodecCtx, frame);
            if (ret == AVERROR_EOF) {
                mIsRunning = false;
            }
            if (ret < 0) {
                break;
            }
            AVFrame* clonedFrame = av_frame_alloc();
            av_frame_ref(clonedFrame, frame);
            int64_t pts =
                (frame->pts == AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
            pts = static_cast<int64_t>(pts * av_q2d(mTimeBase) * 1000);
            int64_t duration = (frame->duration == AV_NOPTS_VALUE)
                                   ? 0
                                   : frame->duration * av_q2d(mTimeBase) * 1000;
            clonedFrame->pts = pts;
            clonedFrame->duration = duration;
            auto videoFrame = std::make_shared<FrameHandle>(clonedFrame);
            mFrameQueue->push(videoFrame);
        }
    }

    av_freep(&rgbFrame->data[0]);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}  // namespace yffplayer
