#include "VideoFrame.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace yffplayer {

VideoFrame::VideoFrame(void* frameHandle) : frameHandle_(frameHandle) {
    if (frameHandle_) {
        setupDataPointers();
    }
}

VideoFrame::~VideoFrame() {
    if (frameHandle_) {
        releaseFrameHandle(frameHandle_);
        frameHandle_ = nullptr;
    }
}

void VideoFrame::setupDataPointers() {
    if (!frameHandle_) {
        return;
    }

    AVFrame* avFrame = static_cast<AVFrame*>(frameHandle_);

    for (int i = 0; i < NUM_DATA_POINTERS; ++i) {
        data_[i] = avFrame->data[i];
        linesize_[i] = avFrame->linesize[i];
    }

    width_ = avFrame->width;
    height_ = avFrame->height;

    pts_ = (avFrame->pts == AV_NOPTS_VALUE) ? avFrame->best_effort_timestamp : avFrame->pts;
    duration_ = (avFrame->duration == AV_NOPTS_VALUE) ? 0 : avFrame->duration;

    switch (avFrame->format) {
        case AV_PIX_FMT_YUV420P:
            format_ = PixelFormat::YUV420P;
            break;
        case AV_PIX_FMT_NV12:
            format_ = PixelFormat::NV12;
            break;
        case AV_PIX_FMT_RGB24:
            format_ = PixelFormat::RGB24;
            break;
        case AV_PIX_FMT_VIDEOTOOLBOX:
            format_ = PixelFormat::VIDEOTOOLBOX;
            break;
        default:
            format_ = PixelFormat::YUV420P;  // 默认格式
            break;
    }

    isKeyFrame_ = avFrame->flags & AV_FRAME_FLAG_KEY;
}

VideoFrame::VideoFrame(VideoFrame&& other) noexcept
    : pts_(other.pts_),
      duration_(other.duration_),
      width_(other.width_),
      height_(other.height_),
      format_(other.format_),
      isKeyFrame_(other.isKeyFrame_),
      frameHandle_(other.frameHandle_) {
    for (int i = 0; i < NUM_DATA_POINTERS; ++i) {
        linesize_[i] = other.linesize_[i];
    }
    std::copy(other.data_, other.data_ + 4, data_);
    other.frameHandle_ = nullptr;
    std::fill(other.data_, other.data_ + 4, nullptr);
}

void VideoFrame::releaseFrameHandle(void* handle) {
    if (handle) {
        AVFrame* avFrame = static_cast<AVFrame*>(handle);
        av_frame_free(&avFrame);
    }
}

// 移动赋值操作符
VideoFrame& VideoFrame::operator=(VideoFrame&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (frameHandle_) {
            releaseFrameHandle(frameHandle_);
        }

        // 移动数据
        pts_ = other.pts_;
        duration_ = other.duration_;
        width_ = other.width_;
        height_ = other.height_;
        format_ = other.format_;
        isKeyFrame_ = other.isKeyFrame_;
        frameHandle_ = other.frameHandle_;
        for (int i = 0; i < NUM_DATA_POINTERS; ++i) {
            linesize_[i] = other.linesize_[i];
        }

        std::copy(other.data_, other.data_ + 4, data_);

        // 清空源对象
        other.frameHandle_ = nullptr;
        std::fill(other.data_, other.data_ + 4, nullptr);
    }
    return *this;
}

// 获取指定平面的数据指针
uint8_t* VideoFrame::getData(int plane) const {
    if (plane >= 0 && plane < 4) {
        return data_[plane];
    }
    return nullptr;
}

// 获取指定平面的行步长
int VideoFrame::getLinesize(int plane) const {
    if (plane >= 0 && plane < 4) {
        return linesize_[plane];
    }
    return 0;
}

// 检查帧是否有效
bool VideoFrame::isValid() const { return frameHandle_ != nullptr && width_ > 0 && height_ > 0; }

}  // namespace yffplayer
