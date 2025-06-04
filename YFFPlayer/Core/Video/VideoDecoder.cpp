#include "VideoDecoder.h"

#include <iostream>

#if defined(__APPLE__)
#include <pthread.h>
#endif

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
    std::cerr << "~VideoDecoder" << std::endl;
}

bool VideoDecoder::open(AVCodecParameters* codecParams, AVRational timeBase) {
    mTimeBase = timeBase;

    mCodecCtx = avcodec_alloc_context3(nullptr);
    if (!mCodecCtx) {
        std::cerr << "Failed to allocate video codec context" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(mCodecCtx, codecParams) < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        std::cerr << "Video codec not found" << std::endl;
        return false;
    }
    if (codecParams->codec_id == AV_CODEC_ID_H264 || codecParams->codec_id == AV_CODEC_ID_HEVC) {
        mCodecCtx->get_format = CodecContextGetFormat;
        mCodecCtx->codec_id = codec->id;
    }

    if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        return false;
    }

    return true;
}

void VideoDecoder::start() {
    mIsRunning = true;
    mPaused = false;
    mDecodeThread = std::thread(&VideoDecoder::decodeLoop, this);
    std::cout << "VideoDecoder started" << std::endl;
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
    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
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
            std::cerr << "Received empty packet, skipping\n";
            continue;
        }

        // 确保从关键帧开始
        static bool firstPacket = true;
        if (firstPacket && !(avPacket->flags & AV_PKT_FLAG_KEY)) {
            std::cerr << "Skipping non-keyframe packet at start\n";
            continue;
        }
        firstPacket = false;

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
            AVPixelFormat sourceFormat = (AVPixelFormat)frame->format;
            // 计算时间戳
            int64_t pts =
                (frame->pts == AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
            pts = static_cast<int64_t>(pts * av_q2d(mTimeBase) * 1000);
            int64_t dur = (frame->duration == AV_NOPTS_VALUE)
                              ? 0
                              : frame->duration * av_q2d(mTimeBase) * 1000;

            // 对于不支持的格式，需要转换为RGB24
            if (sourceFormat != AV_PIX_FMT_YUV420P && sourceFormat != AV_PIX_FMT_NV12 &&
                sourceFormat != AV_PIX_FMT_RGB24 && sourceFormat != AV_PIX_FMT_VIDEOTOOLBOX) {
                if (!mSwsCtx) {
                    mSwsCtx = sws_getContext(frame->width, frame->height, sourceFormat,
                                             frame->width, frame->height, AV_PIX_FMT_RGB24,
                                             SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                    av_image_alloc(rgbFrame->data, rgbFrame->linesize, frame->width, frame->height,
                                   AV_PIX_FMT_RGB24, 1);
                }
                sws_scale(mSwsCtx, frame->data, frame->linesize, 0, frame->height, rgbFrame->data,
                          rgbFrame->linesize);

                std::array<int, 4> lineSize;
                int linesizes[4] = {0};
                av_image_fill_linesizes(linesizes, AV_PIX_FMT_RGB24, frame->width);
                lineSize = {linesizes[0], 0, 0, 0};

                int bufferSize =
                    av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame->width, frame->height, 1);
                std::vector<uint8_t> data(bufferSize);
                av_image_copy_to_buffer(data.data(), bufferSize, rgbFrame->data, rgbFrame->linesize,
                                        AV_PIX_FMT_RGB24, frame->width, frame->height, 1);

                rgbFrame->pts = pts;
                rgbFrame->duration = dur;
                mFrameQueue->push(std::make_shared<FrameHandle>(rgbFrame));
            } else {
                AVFrame* clonedFrame = av_frame_clone(frame);
                if (clonedFrame) {
                    clonedFrame->pts = pts;
                    clonedFrame->duration = dur;
                    auto videoFrame = std::make_shared<FrameHandle>(clonedFrame);
                    mFrameQueue->push(videoFrame);
                }
            }
        }
    }

    av_freep(&rgbFrame->data[0]);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}  // namespace yffplayer
