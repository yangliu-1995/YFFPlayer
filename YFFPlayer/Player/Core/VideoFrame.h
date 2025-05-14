#pragma once

#include <memory>

namespace yffplayer {

enum class PixelFormat { YUV420P, RGB24, NV12 };

struct VideoFrame {
    uint8_t* data[3];    // Data pointers
    int linesize[3];     // Line sizes
    int width;           // Width
    int height;          // Height
    int64_t pts;         // Timestamp
    int64_t duration;    // Duration
    PixelFormat format;  // Pixel format
};

}  // namespace yffplayer
