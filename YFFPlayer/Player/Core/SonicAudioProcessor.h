#pragma once

#include <memory>
#include <vector>

#include "AudioFrame.h"

// Sonic 库的 C 接口
extern "C" {
#include "sonic.h"
}

namespace yffplayer {

class SonicAudioProcessor {
public:
    SonicAudioProcessor();
    ~SonicAudioProcessor();

    bool initialize(int sampleRate, int channels);
    void setPlaybackRate(float rate);
    void setPitch(float pitch = 1.0f);  // 可选：保持音调不变

    // 处理音频帧，返回处理后的音频帧
    std::unique_ptr<AudioFrame> processAudioFrame(const AudioFrame& inputFrame);

    void flush();
    void reset();

private:
    sonicStream mSonicStream;
    int mSampleRate;
    int mChannels;
    float mCurrentRate;
    bool mInitialized;

    // 内部缓冲区
    std::vector<short> mInputBuffer;
    std::vector<short> mOutputBuffer;

    // 转换函数
    void floatToShort(const float* input, short* output, size_t samples);
    void shortToFloat(const short* input, float* output, size_t samples);
};

}  // namespace yffplayer
