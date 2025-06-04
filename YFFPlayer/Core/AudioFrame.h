#pragma once

#include <cstdint>
#include <vector>

namespace yffplayer {

struct AudioFrame {
    int64_t mPts = 0;
    int64_t mDuration = 0;
    int mSampleRate = 0;
    int mChannels = 0;
    int mNbSamples = 0;  // 每帧的采样点数
    std::vector<uint8_t> mData;

    AudioFrame() = default;

    // 构造函数
    AudioFrame(int64_t pts, int64_t duration, int sampleRate, int channels, int nbSamples,
               std::vector<uint8_t>&& data)
        : mPts(pts),
          mDuration(duration),
          mSampleRate(sampleRate),
          mChannels(channels),
          mNbSamples(nbSamples),
          mData(std::move(data)) {}
};

}  // namespace yffplayer
