#include "VideoDecoder.h"
#include <iostream>
#include <vector>

#include "Packet.h"

namespace yffplayer {

VideoDecoder::VideoDecoder(std::shared_ptr<PacketQueue> packetQueue,
                           std::shared_ptr<FrameQueue<VideoFrame>> frameQueue)
    : mPacketQueue(std::move(packetQueue)), mFrameQueue(std::move(frameQueue)) {
}

VideoDecoder::~VideoDecoder() {
    stop();
    if (mSwsCtx) sws_freeContext(mSwsCtx);
    if (mCodecCtx) avcodec_free_context(&mCodecCtx);
}

bool VideoDecoder::open(AVCodecParameters* codecParams, AVRational timeBase) {
    mTimeBase = timeBase;
    
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        std::cerr << "Video codec not found" << std::endl;
        return false;
    }

    mCodecCtx = avcodec_alloc_context3(codec);
    if (!mCodecCtx) {
        std::cerr << "Failed to allocate video codec context" << std::endl;
        return false;
    }
    
    if (avcodec_parameters_to_context(mCodecCtx, codecParams) < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return false;
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
    resume(); // 防止线程阻塞在暂停状态
    if (mDecodeThread.joinable()) {
        mDecodeThread.join();
    }
}

void VideoDecoder::pause() {
    mPaused = true;
}

void VideoDecoder::resume() {
    mPaused = false;
    mCond.notify_all();
}

int64_t VideoDecoder::toMs(int64_t pts, AVRational timeBase) {
    return av_rescale_q(pts, timeBase, AVRational{1, 1000});
}

void VideoDecoder::decodeLoop() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    while (mIsRunning) {
        // 处理暂停逻辑
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, [this] {
            return !mPaused || mIsRunning;
        });
        lock.unlock();
        
        if (!mIsRunning) {
            break;
        }
        
        auto pkt = mPacketQueue->pop();
        if (!pkt) break;
        auto avPacket = pkt->mPacket;
        if (!avPacket) {
            std::cerr << "Received empty packet, skipping\n";
            continue;
        }

        if (avcodec_send_packet(mCodecCtx, avPacket) < 0) continue;

        int ret = avcodec_receive_frame(mCodecCtx, frame);

        while (ret == 0) {
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
                    // 计算实际需要的内存大小（紧凑格式）
                    int ySize = frame->width * frame->height;
                    int uSize = (frame->width/2) * (frame->height/2);
                    int vSize = (frame->width/2) * (frame->height/2);
                    data.resize(ySize + uSize + vSize);
                    uint8_t* dst = data.data();
                    // 拷贝 Y plane - 每行只拷贝有效像素
                    for (int i = 0; i < frame->height; ++i) {
                        memcpy(dst, frame->data[0] + i * frame->linesize[0], frame->width);
                        dst += frame->width;
                    }
                    // 拷贝 U plane
                    for (int i = 0; i < frame->height/2; ++i) {
                        memcpy(dst, frame->data[1] + i * frame->linesize[1], frame->width/2);
                        dst += frame->width/2;
                    }
                    // 拷贝 V plane
                    for (int i = 0; i < frame->height/2; ++i) {
                        memcpy(dst, frame->data[2] + i * frame->linesize[2], frame->width/2);
                        dst += frame->width/2;
                    }
                } else if (sourceFormat == AV_PIX_FMT_NV12) {
                    pixelFormat = PixelFormat::NV12;
                    // 计算实际需要的内存大小（紧凑格式）
                    int ySize = frame->width * frame->height;
                    int uvSize = frame->width * (frame->height / 2); // NV12的UV平面是交错的，宽度与Y相同
                    data.resize(ySize + uvSize);
                    uint8_t* dst = data.data();

                    // 拷贝 Y plane - 每行只拷贝有效像素
                    for (int i = 0; i < frame->height; ++i) {
                        memcpy(dst, frame->data[0] + i * frame->linesize[0], frame->width);
                        dst += frame->width;
                    }

                    // 拷贝 interleaved UV plane - 注意UV是交错存储的
                    for (int i = 0; i < frame->height / 2; ++i) {
                        memcpy(dst, frame->data[1] + i * frame->linesize[1], frame->width);
                        dst += frame->width;
                    }
                } else if (sourceFormat == AV_PIX_FMT_RGB24) {
                    pixelFormat = PixelFormat::RGB24;
                    int rowSize = frame->width * 3; // RGB24 每像素3字节
                    data.resize(frame->height * rowSize);
                    uint8_t* dst = data.data();

                    // 确保只拷贝有效像素数据
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
            ret = avcodec_receive_frame(mCodecCtx, frame);
            if (ret == AVERROR_EOF) {
                mIsRunning = false;
            }
        }
    }

    av_freep(&rgbFrame->data[0]);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

} // namespace yffplayer
