#pragma once

#include <memory>
#include <vector>

#include "AudioFrame.h"
#include "AudioResampleContext.h"
#include "FrameHandle.h"

// Sonic 库的 C 接口
extern "C" {
#include "sonic.h"
}

// FFmpeg 重采样库
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace yffplayer {

class AudioFrameProcessor {
public:
    AudioFrameProcessor();
    ~AudioFrameProcessor();

    bool initialize(int sampleRate, int channels, int format);
    void setPlaybackRate(float rate);
    void setPitch(float pitch = 1.0f);  // 可选：保持音调不变

    std::unique_ptr<AudioFrame> processAudioFrame(const std::shared_ptr<FrameHandle> frameHandle, double delay);

    void flush();
    void reset();

private:
    sonicStream mSonicStream;
    int mSampleRate;
    int mChannels;
    int mFormat;
    float mCurrentRate;
    bool mInitialized;
    std::unique_ptr<AudioResampleContext> mResampleContext;

    // 内部缓冲区
    std::vector<short> mInputBuffer;
    std::vector<short> mOutputBuffer;
    std::vector<uint8_t*> mSwrInputData;
    std::vector<uint8_t*> mSwrOutputData;

    // 转换函数
    void floatToShort(const float* input, short* output, size_t samples);
    void shortToFloat(const short* input, float* output, size_t samples);

    int calculateWantedSamples(int nbSamples, double delay, int sampleRate);
    std::unique_ptr<AudioFrame> reSampleAVFrame(const AVFrame &frame, int wantedSamples);
};

}  // namespace yffplayer
