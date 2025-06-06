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
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace yffplayer {

class AudioFrameProcessor {
public:
    AudioFrameProcessor();
    ~AudioFrameProcessor();

    bool initialize(int sampleRate, int channels, int format);
    void setPlaybackRate(float rate);
    void setPitch(float pitch = 1.0f);  // 可选：保持音调不变

    std::unique_ptr<AudioFrame> processAudioFrame(const std::shared_ptr<FrameHandle> frameHandle,
                                                  double delay);

    void flush();
    void reset();

private:
    sonicStream sonicStream_;
    int sampleRate_;
    int channels_;
    int format_;
    float currentRate_;
    bool initialized_;
    std::unique_ptr<AudioResampleContext> resampleContext_;

    // 内部缓冲区
    std::vector<short> inputBuffer_;
    std::vector<short> outputBuffer_;
    std::vector<uint8_t*> swrInputData_;
    std::vector<uint8_t*> swrOutputData_;

    // 转换函数
    void floatToShort(const float* input, short* output, size_t samples);
    void shortToFloat(const short* input, float* output, size_t samples);

    std::unique_ptr<AudioFrame> reSampleAVFrame(const AVFrame& frame);
};

}  // namespace yffplayer
