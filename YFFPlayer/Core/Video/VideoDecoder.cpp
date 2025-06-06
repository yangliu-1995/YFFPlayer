#include "VideoDecoder.h"

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
    : packetQueue_(std::move(packetQueue)), frameQueue_(std::move(frameQueue)) {}

VideoDecoder::~VideoDecoder() {
    stop();
    LogInfo << "~VideoDecoder";
}

bool VideoDecoder::open(AVCodecParameters* codecParams, AVRational timeBase) {
    timeBase_ = timeBase;

    codecCtx_ = avcodec_alloc_context3(nullptr);
    if (!codecCtx_) {
        LogInfo << "Failed to allocate video codec context";
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecParams) < 0) {
        LogInfo << "Failed to copy codec parameters";
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        LogInfo << "Video codec not found";
        return false;
    }
    if (codecParams->codec_id == AV_CODEC_ID_H264 || codecParams->codec_id == AV_CODEC_ID_HEVC) {
        codecCtx_->get_format = CodecContextGetFormat;
        codecCtx_->codec_id = codec->id;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        LogInfo << "Failed to open codec";
        return false;
    }

    return true;
}

void VideoDecoder::start() {
    isRunning_ = true;
    paused_ = false;
    decodeThread_ = std::thread(&VideoDecoder::decodeLoop, this);
    LogInfo << "VideoDecoder started";
}

void VideoDecoder::stop() {
    isRunning_ = false;
    resume();  // 防止线程阻塞在暂停状态
    if (decodeThread_.joinable()) {
        decodeThread_.join();
    }
    if (codecCtx_ && avcodec_is_open(codecCtx_)) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
}

void VideoDecoder::pause() { paused_ = true; }

void VideoDecoder::resume() {
    paused_ = false;
    cond_.notify_all();
}

void VideoDecoder::flush() {
    frameQueue_->clear();
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }
}

void VideoDecoder::decodeLoop() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.decoder.video");
#endif
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    while (isRunning_) {
        // 处理暂停逻辑
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !paused_; });
        lock.unlock();

        if (!isRunning_) {
            break;
        }

        auto pkt = packetQueue_->pop();
        if (!pkt) {
            continue;
        }

        auto avPacket = pkt->packet_;
        if (!avPacket) {
            LogInfo << "Received empty packet, skipping\n";
            continue;
        }

        int ret = avcodec_send_packet(codecCtx_, avPacket);
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
            int ret = avcodec_receive_frame(codecCtx_, frame);
            if (ret == AVERROR_EOF) {
                isRunning_ = false;
            }
            if (ret < 0) {
                break;
            }
            AVFrame* clonedFrame = av_frame_alloc();
            av_frame_ref(clonedFrame, frame);
            int64_t pts =
                (frame->pts == AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
            pts = static_cast<int64_t>(pts * av_q2d(timeBase_) * 1000);
            int64_t duration = (frame->duration == AV_NOPTS_VALUE)
                                   ? 0
                                   : frame->duration * av_q2d(timeBase_) * 1000;
            clonedFrame->pts = pts;
            clonedFrame->duration = duration;
            auto videoFrame = std::make_shared<FrameHandle>(clonedFrame);
            frameQueue_->push(videoFrame);
        }
    }

    av_freep(&rgbFrame->data[0]);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}  // namespace yffplayer
