#pragma once

#include <cstdint>
#include <vector>

namespace yffplayer {

enum class PixelFormat {
    YUV420P,
    NV12,
    RGB24
};

struct VideoFrame {
    int64_t mPts = 0;
    int64_t mDuration = 0;
    int mWidth = 0;
    int mHeight = 0;
    PixelFormat mFormat = PixelFormat::YUV420P;
    std::vector<uint8_t> mData;
    bool mIsKeyFrame = false;

    VideoFrame() = default;

    VideoFrame(int64_t pts, int64_t duration, int width, int height,
               PixelFormat format, std::vector<uint8_t>&& data, bool isKeyFrame)
        : mPts(pts),
          mDuration(duration),
          mWidth(width),
          mHeight(height),
          mFormat(format),
          mData(std::move(data)),
          mIsKeyFrame(isKeyFrame) {}
};

} // namespace yffplayer
