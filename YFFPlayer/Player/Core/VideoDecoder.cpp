#include "VideoDecoder.h"
#include <iostream>
#include <vector>

#include "Packet.h"

namespace yffplayer {

VideoDecoder::VideoDecoder(std::shared_ptr<PacketQueue> packetQueue,
                           std::shared_ptr<FrameQueue<VideoFrame>> frameQueue,
                           AVCodecParameters* codecParams,
                           AVRational timeBase)
    : mPacketQueue(std::move(packetQueue)), mFrameQueue(std::move(frameQueue)), mTimeBase(timeBase) {
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) throw std::runtime_error("Video codec not found");

    mCodecCtx = avcodec_alloc_context3(codec);
    if (!mCodecCtx) throw std::runtime_error("Failed to allocate video codec context");
    avcodec_parameters_to_context(mCodecCtx, codecParams);
    avcodec_open2(mCodecCtx, codec, nullptr);

    mSwsCtx = nullptr; // lazy init
}

VideoDecoder::~VideoDecoder() {
    if (mSwsCtx) sws_freeContext(mSwsCtx);
    if (mCodecCtx) avcodec_free_context(&mCodecCtx);
}

void VideoDecoder::start() {
    mDecodeThread = std::thread(&VideoDecoder::decodeLoop, this);
    std::cout << "VideoDecoder started\n";
}

int64_t VideoDecoder::toMs(int64_t pts, AVRational timeBase) {
    return av_rescale_q(pts, timeBase, AVRational{1, 1000});
}

void VideoDecoder::decodeLoop() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    while (!mStopped) {
        auto pkt = mPacketQueue->pop();
        if (!pkt) break;
        auto avPacket = pkt->mPacket;
        if (!avPacket) {
            std::cerr << "Received empty packet, skipping\n";
            continue;
        }


        if (avcodec_send_packet(mCodecCtx, avPacket) < 0) continue;

        while (avcodec_receive_frame(mCodecCtx, frame) == 0) {
            AVPixelFormat sourceFormat = (AVPixelFormat)frame->format;
            if (sourceFormat != AV_PIX_FMT_YUV420P && sourceFormat != AV_PIX_FMT_NV12 && sourceFormat != AV_PIX_FMT_RGB24) {
                if (!mSwsCtx) {
                    mSwsCtx = sws_getContext(
                        frame->width, frame->height, (AVPixelFormat)frame->format,
                        frame->width, frame->height, AV_PIX_FMT_RGB24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr);
                    av_image_alloc(rgbFrame->data, rgbFrame->linesize, frame->width, frame->height, AV_PIX_FMT_RGB24, 1);
                }
                sws_scale(mSwsCtx, frame->data, frame->linesize, 0, frame->height,
                            rgbFrame->data, rgbFrame->linesize);
                int size = frame->height * rgbFrame->linesize[0];
                std::vector<uint8_t> data(rgbFrame->data[0], rgbFrame->data[0] + size);
                int64_t pts = (frame->pts == AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                pts = static_cast<int64_t>(pts * av_q2d(mTimeBase) * 1000);
                int64_t dur = (frame->duration == AV_NOPTS_VALUE) ? 0 : frame->duration * av_q2d(mTimeBase) * 1000;
                bool isKey = frame->key_frame == 1;
                mFrameQueue->push(std::make_shared<VideoFrame>(
                    pts, dur, frame->width, frame->height, PixelFormat::RGB24, std::move(data), isKey));
            } else {
                std::vector<uint8_t> data;
                PixelFormat pixelFormat;
                if (sourceFormat == AV_PIX_FMT_YUV420P) {
                    pixelFormat = PixelFormat::YUV420P;
                    int ySize = frame->height * frame->linesize[0];
                    int uSize = frame->height / 2 * frame->linesize[1];
                    int vSize = frame->height / 2 * frame->linesize[2];
                    data.resize(ySize + uSize + vSize);
                    uint8_t* dst = data.data();

                    // 拷贝 Y plane
                    for (int i = 0; i < frame->height; ++i) {
                        memcpy(dst, frame->data[0] + i * frame->linesize[0], frame->width);
                        dst += frame->width;
                    }

                    // 拷贝 U plane
                    for (int i = 0; i < frame->height / 2; ++i) {
                        memcpy(dst, frame->data[1] + i * frame->linesize[1], frame->width / 2);
                        dst += frame->width / 2;
                    }

                    // 拷贝 V plane
                    for (int i = 0; i < frame->height / 2; ++i) {
                        memcpy(dst, frame->data[2] + i * frame->linesize[2], frame->width / 2);
                        dst += frame->width / 2;
                    }
                } else if (sourceFormat == AV_PIX_FMT_NV12) {
                    pixelFormat = PixelFormat::NV12;
                    int ySize = frame->height * frame->linesize[0];
                    int uvSize = frame->height / 2 * frame->linesize[1];
                    data.resize(ySize + uvSize);
                    uint8_t* dst = data.data();

                    // 拷贝 Y plane
                    for (int i = 0; i < frame->height; ++i) {
                        memcpy(dst, frame->data[0] + i * frame->linesize[0], frame->width);
                        dst += frame->width;
                    }

                    // 拷贝 interleaved UV plane
                    for (int i = 0; i < frame->height / 2; ++i) {
                        memcpy(dst, frame->data[1] + i * frame->linesize[1], frame->width);
                        dst += frame->width;
                    }
                } else if (sourceFormat == AV_PIX_FMT_RGB24) {
                    pixelFormat = PixelFormat::RGB24;
                    int rowSize = frame->width * 3; // RGB24 每像素3字节
                    data.resize(frame->height * rowSize);
                    uint8_t* dst = data.data();

                    for (int i = 0; i < frame->height; ++i) {
                        memcpy(dst, frame->data[0] + i * frame->linesize[0], rowSize);
                        dst += rowSize;
                    }
                } else {
                    // fallback（一般不进入，因为这部分是 if else 分支）
                    pixelFormat = PixelFormat::RGB24;
                    data.clear();
                }

                int64_t pts = (frame->pts == AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
                pts = static_cast<int64_t>(pts * av_q2d(mTimeBase) * 1000);
                int64_t dur = (frame->duration == AV_NOPTS_VALUE) ? 0 : frame->duration * av_q2d(mTimeBase) * 1000;
                bool isKey = frame->key_frame == 1;

                mFrameQueue->push(std::make_shared<VideoFrame>(
                    pts, dur, frame->width, frame->height, pixelFormat, std::move(data), isKey));
            }
        }
    }

    av_freep(&rgbFrame->data[0]);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

void VideoDecoder::stop() {
    mStopped = true;
}

} // namespace yffplayer
