#pragma once

#include <cstdint>
#include <vector>

namespace yffplayer {

struct AudioFrame {
    int64_t pts_ = 0;
    int64_t duration_ = 0;
    int sampleRate_ = 0;
    int channels_ = 0;
    int nbSamples_ = 0;  // 每帧的采样点数
    std::vector<uint8_t> data_;

    AudioFrame() = default;

    // 构造函数
    AudioFrame(int64_t pts, int64_t duration, int sampleRate, int channels, int nbSamples,
               std::vector<uint8_t>&& data)
        : pts_(pts),
          duration_(duration),
          sampleRate_(sampleRate),
          channels_(channels),
          nbSamples_(nbSamples),
          data_(std::move(data)) {}
};

}  // namespace yffplayer
