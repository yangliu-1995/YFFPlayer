#pragma once

#include <memory>

namespace yffplayer {

enum class PixelFormat { YUV420P, RGB24, NV12 };

struct VideoFrame {
    uint8_t* data[3];    // 数据指针
    int linesize[3];     // 行大小
    int width;           // 宽度
    int height;          // 高度
    int64_t pts;         // 时间戳
    int64_t duration;    // 持续时间
    PixelFormat format;  // 像素格式
};

}  // namespace yffplayer
