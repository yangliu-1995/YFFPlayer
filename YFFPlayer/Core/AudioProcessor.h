#pragma once

#include <memory>
#include <vector>

#include "AudioFrame.h"
#include "FrameHandle.h"

// Sonic 库的 C 接口
extern "C" {
#include "sonic.h"
}

// FFmpeg 重采样库
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace yffplayer {

class AudioProcessor {
public:
    AudioProcessor();
    ~AudioProcessor();

    bool initialize(int sampleRate, int channels);
    void setPlaybackRate(float rate);
    void setPitch(float pitch = 1.0f);  // 可选：保持音调不变

    // 处理音频帧，返回处理后的音频帧
    std::unique_ptr<AudioFrame> processAudioFrame(const AudioFrame& inputFrame);
    
    // 处理FrameHandle，将其转换为AudioFrame并进行处理
    std::unique_ptr<AudioFrame> processFrameHandle(const FrameHandle& frameHandle);
    
    // 重采样到指定的采样数量
    std::unique_ptr<AudioFrame> resampleToWantedSamples(const AudioFrame& inputFrame, int wantedSamples);

    void flush();
    void reset();

private:
    sonicStream mSonicStream;
    int mSampleRate;
    int mChannels;
    float mCurrentRate;
    bool mInitialized;
    
    // FFmpeg 重采样上下文
    SwrContext* mSwrContext;
    bool mSwrInitialized;

    // 内部缓冲区
    std::vector<short> mInputBuffer;
    std::vector<short> mOutputBuffer;
    std::vector<uint8_t*> mSwrInputData;
    std::vector<uint8_t*> mSwrOutputData;

    // 转换函数
    void floatToShort(const float* input, short* output, size_t samples);
    void shortToFloat(const short* input, float* output, size_t samples);
};

}  // namespace yffplayer
