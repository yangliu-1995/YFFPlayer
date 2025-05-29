#include "VideoFrame.h"

extern "C" {
#include <libavutil/frame.h>
}



namespace yffplayer {

VideoFrame::VideoFrame(void* frameHandle) : mFrameHandle(frameHandle) {
    if (mFrameHandle) {
        setupDataPointers();
    }
}

void VideoFrame::setupDataPointers() {
    if (!mFrameHandle) {
        return;
    }
    
    AVFrame* avFrame = static_cast<AVFrame*>(mFrameHandle);

    for (int i = 0; i < NUM_DATA_POINTERS; ++i) {
        mData[i] = avFrame->data[i];
        mLinesize[i] = avFrame->linesize[i];
    }

    mWidth = avFrame->width;
    mHeight = avFrame->height;

    mPts = (avFrame->pts == AV_NOPTS_VALUE) ? avFrame->best_effort_timestamp : avFrame->pts;
    mDuration = (avFrame->duration == AV_NOPTS_VALUE) ? 0 : avFrame->duration;

    switch (avFrame->format) {
        case AV_PIX_FMT_YUV420P:
            mFormat = PixelFormat::YUV420P;
            break;
        case AV_PIX_FMT_NV12:
            mFormat = PixelFormat::NV12;
            break;
        case AV_PIX_FMT_RGB24:
            mFormat = PixelFormat::RGB24;
            break;
        case AV_PIX_FMT_VIDEOTOOLBOX:
            mFormat = PixelFormat::VIDEOTOOLBOX;
            break;
        default:
            mFormat = PixelFormat::YUV420P; // 默认格式
            break;
    }

    mIsKeyFrame = (avFrame->key_frame == 1);
}

VideoFrame::VideoFrame(VideoFrame&& other) noexcept
    : mPts(other.mPts), mDuration(other.mDuration),
      mWidth(other.mWidth), mHeight(other.mHeight),
      mFormat(other.mFormat), mIsKeyFrame(other.mIsKeyFrame),
      mFrameHandle(other.mFrameHandle) {
          for (int i = 0; i < NUM_DATA_POINTERS; ++i) {
              mLinesize[i] = other.mLinesize[i];
          }
    std::copy(other.mData, other.mData + 4, mData);
    other.mFrameHandle = nullptr;
    std::fill(other.mData, other.mData + 4, nullptr);
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
        if (mFrameHandle) {
            releaseFrameHandle(mFrameHandle);
        }
        
        // 移动数据
        mPts = other.mPts;
        mDuration = other.mDuration;
        mWidth = other.mWidth;
        mHeight = other.mHeight;
        mFormat = other.mFormat;
        mIsKeyFrame = other.mIsKeyFrame;
        mFrameHandle = other.mFrameHandle;
        for (int i = 0; i < NUM_DATA_POINTERS; ++i) {
            mLinesize[i] = other.mLinesize[i];
        }

        std::copy(other.mData, other.mData + 4, mData);
        
        // 清空源对象
        other.mFrameHandle = nullptr;
        std::fill(other.mData, other.mData + 4, nullptr);
    }
    return *this;
}



// 获取指定平面的数据指针
uint8_t* VideoFrame::getData(int plane) const {
    if (plane >= 0 && plane < 4) {
        return mData[plane];
    }
    return nullptr;
}

// 获取指定平面的行步长
int VideoFrame::getLinesize(int plane) const {
    if (plane >= 0 && plane < 4) {
        return mLinesize[plane];
    }
    return 0;
}

// 检查帧是否有效
bool VideoFrame::isValid() const {
    return mFrameHandle != nullptr && mWidth > 0 && mHeight > 0;
}

}  // namespace yffplayer
