#pragma once

#include <memory>

namespace yffplayer {
struct AudioFrame {
    int64_t pts;       // PTS时间戳
    int64_t duration;  // 持续时间
    int64_t size;      // 数据大小
    uint8_t* data;     // 数据指针
    int channels;      // 声道数
    int sampleRate;    // 采样率
    int bitDepth;      // 位深度
};
}  // namespace yffplayer
