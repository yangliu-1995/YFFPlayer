#pragma once

#include <cstdint>
#include <vector>

#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif

namespace yffplayer {

enum class PixelFormat { YUV420P, NV12, RGB24, VIDEOTOOLBOX };

struct VideoFrame {
    int64_t mPts = 0;
    int64_t mDuration = 0;
    int mWidth = 0;
    int mHeight = 0;
    PixelFormat mFormat = PixelFormat::YUV420P;
    std::vector<uint8_t> mData;
    bool mIsKeyFrame = false;
    std::array<int, 4> mLinesize;
    
#if defined(__APPLE__)
    // VideoToolbox 专用字段
    CVPixelBufferRef mPixelBuffer = nullptr;
#endif

    VideoFrame() = default;

    VideoFrame(int64_t pts, int64_t duration, int width, int height, PixelFormat format,
               std::vector<uint8_t> data, const std::array<int, 4>& linesize, bool isKeyFrame)
        : mPts(pts),
          mDuration(duration),
          mWidth(width),
          mHeight(height),
          mFormat(format),
          mData(std::move(data)),
          mLinesize(linesize),
          mIsKeyFrame(isKeyFrame) {}
          
#if defined(__APPLE__)
    // 析构函数中释放 CVPixelBufferRef
    ~VideoFrame() {
        if (mPixelBuffer) {
            CFRelease(mPixelBuffer);
            mPixelBuffer = nullptr;
        }
    }
    
    // 拷贝构造函数
    VideoFrame(const VideoFrame& other) 
        : mPts(other.mPts),
          mDuration(other.mDuration),
          mWidth(other.mWidth),
          mHeight(other.mHeight),
          mFormat(other.mFormat),
          mData(other.mData),
          mLinesize(other.mLinesize),
          mIsKeyFrame(other.mIsKeyFrame),
          mPixelBuffer(other.mPixelBuffer) {
        if (mPixelBuffer) {
            CFRetain(mPixelBuffer);
        }
    }
    
    // 赋值操作符
    VideoFrame& operator=(const VideoFrame& other) {
        if (this != &other) {
            if (mPixelBuffer) {
                CFRelease(mPixelBuffer);
            }
            
            mPts = other.mPts;
            mDuration = other.mDuration;
            mWidth = other.mWidth;
            mHeight = other.mHeight;
            mFormat = other.mFormat;
            mData = other.mData;
            mLinesize = other.mLinesize;
            mIsKeyFrame = other.mIsKeyFrame;
            mPixelBuffer = other.mPixelBuffer;
            
            if (mPixelBuffer) {
                CFRetain(mPixelBuffer);
            }
        }
        return *this;
    }
#endif
};

}  // namespace yffplayer
