#include "AudioProcessor.h"

#include <algorithm>
#include <cstring>
#include <iostream>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace yffplayer {

AudioProcessor::AudioProcessor()
    : mSonicStream(nullptr),
      mSampleRate(0),
      mChannels(0),
      mCurrentRate(1.0f),
      mInitialized(false),
      mSwrContext(nullptr),
      mSwrInitialized(false) {}

AudioProcessor::~AudioProcessor() {
    if (mSonicStream) {
        sonicDestroyStream(mSonicStream);
        mSonicStream = nullptr;
    }
    
    if (mSwrContext) {
        swr_free(&mSwrContext);
        mSwrContext = nullptr;
    }
}

bool AudioProcessor::initialize(int sampleRate, int channels) {
    if (mSonicStream) {
        sonicDestroyStream(mSonicStream);
    }

    mSampleRate = sampleRate;
    mChannels = channels;

    mSonicStream = sonicCreateStream(sampleRate, channels);
    if (!mSonicStream) {
        std::cerr << "Failed to create Sonic stream" << std::endl;
        return false;
    }

    // 设置默认参数
    sonicSetSpeed(mSonicStream, mCurrentRate);
    sonicSetPitch(mSonicStream, 1.0f);  // 保持音调不变
    
    // 初始化 SwrContext
    if (mSwrContext) {
        swr_free(&mSwrContext);
    }
    
    mSwrContext = swr_alloc();
    if (!mSwrContext) {
        std::cerr << "Failed to allocate SwrContext" << std::endl;
        return false;
    }
    
    // 设置重采样参数 (44100Hz, 2ch, 16bit)
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, 2);
    
    AVChannelLayout inLayout;
    av_channel_layout_default(&inLayout, 2);
    
    if (swr_alloc_set_opts2(&mSwrContext, &outLayout, AV_SAMPLE_FMT_S16, 44100, &inLayout,
                            AV_SAMPLE_FMT_S16, 44100, 0, nullptr) < 0) {
        std::cerr << "swr_alloc_set_opts2 failed" << std::endl;
        return false;
    }
    
    if (swr_init(mSwrContext) < 0) {
        std::cerr << "Failed to initialize SwrContext" << std::endl;
        swr_free(&mSwrContext);
        mSwrContext = nullptr;
        return false;
    }
    
    mSwrInitialized = true;

    mInitialized = true;
    return true;
}

void AudioProcessor::setPlaybackRate(float rate) {
    if (!mInitialized || !mSonicStream) {
        return;
    }

    mCurrentRate = rate;
    sonicSetSpeed(mSonicStream, rate);
}

void AudioProcessor::setPitch(float pitch) {
    if (!mInitialized || !mSonicStream) {
        return;
    }

    sonicSetPitch(mSonicStream, pitch);
}

std::unique_ptr<AudioFrame> AudioProcessor::processFrameHandle(const FrameHandle& frameHandle) {
    if (!frameHandle.isValid()) {
        return nullptr;
    }
    
    // 从FrameHandle获取AVFrame
    AVFrame* avFrame = frameHandle.getFrame();
    if (!avFrame) {
        return nullptr;
    }
    
    // 从AVFrame提取音频数据
    int64_t pts = (avFrame->pts == AV_NOPTS_VALUE) ? avFrame->best_effort_timestamp : avFrame->pts;
    int sampleRate = avFrame->sample_rate;
    int channels = avFrame->ch_layout.nb_channels;
    int nbSamples = avFrame->nb_samples;
    
    // 计算持续时间（毫秒）
    int64_t duration = 0;
    if (sampleRate > 0) {
        duration = static_cast<int64_t>(nbSamples * 1000.0 / sampleRate);
    }
    
    // 提取音频数据
    std::vector<uint8_t> audioData;
    int dataSize = av_get_bytes_per_sample(static_cast<AVSampleFormat>(avFrame->format)) * channels * nbSamples;
    
    if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(avFrame->format))) {
        // 平面格式：每个声道的数据分别存储
        audioData.resize(dataSize);
        uint8_t* dst = audioData.data();
        int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(avFrame->format));
        
        for (int sample = 0; sample < nbSamples; sample++) {
            for (int ch = 0; ch < channels; ch++) {
                memcpy(dst, avFrame->data[ch] + sample * bytesPerSample, bytesPerSample);
                dst += bytesPerSample;
            }
        }
    } else {
        // 交错格式：所有声道的数据交错存储
        audioData.resize(dataSize);
        memcpy(audioData.data(), avFrame->data[0], dataSize);
    }
    
    // 创建AudioFrame
    auto audioFrame = std::make_unique<AudioFrame>(pts, duration, sampleRate, channels, nbSamples, std::move(audioData));
    
    // 处理AudioFrame
    return processAudioFrame(*audioFrame);
}

std::unique_ptr<AudioFrame> AudioProcessor::processAudioFrame(const AudioFrame& inputFrame) {
    if (!mInitialized || !mSonicStream) {
        return nullptr;
    }

    // 如果播放倍率接近1.0，直接返回原始帧的拷贝
    if (std::abs(mCurrentRate - 1.0f) < 0.01f) {
        auto outputFrame = std::make_unique<AudioFrame>();
        outputFrame->mPts = inputFrame.mPts;
        outputFrame->mDuration = inputFrame.mDuration;
        outputFrame->mSampleRate = inputFrame.mSampleRate;
        outputFrame->mChannels = inputFrame.mChannels;
        outputFrame->mNbSamples = inputFrame.mNbSamples;
        outputFrame->mData = inputFrame.mData;
        return outputFrame;
    }

    int inputSamples = inputFrame.mNbSamples;
    short* inputData = (short*)inputFrame.mData.data();

    // 写入 Sonic 流
    __unused int samplesWritten = sonicWriteShortToStream(mSonicStream, inputData, inputSamples);

    int availableSamples = sonicSamplesAvailable(mSonicStream);
    if (availableSamples <= 0) {
        return nullptr;
    }

    int totalOutputSamples = availableSamples * inputFrame.mChannels;
    mOutputBuffer.resize(totalOutputSamples);

    int samplesRead =
        sonicReadShortFromStream(mSonicStream, mOutputBuffer.data(), availableSamples);

    if (samplesRead <= 0) {
        return nullptr;
    }

    auto outputFrame = std::make_unique<AudioFrame>();
    outputFrame->mPts = inputFrame.mPts;
    outputFrame->mDuration = static_cast<int64_t>(inputFrame.mDuration / mCurrentRate);
    outputFrame->mSampleRate = inputFrame.mSampleRate;
    outputFrame->mChannels = inputFrame.mChannels;
    outputFrame->mNbSamples = samplesRead;

    size_t dataSize = samplesRead * inputFrame.mChannels * sizeof(int16_t);
    outputFrame->mData.resize(dataSize);

    std::memcpy(outputFrame->mData.data(), mOutputBuffer.data(), dataSize);

    return outputFrame;
}

std::unique_ptr<AudioFrame> AudioProcessor::resampleToWantedSamples(const AudioFrame& inputFrame, int wantedSamples) {
    if (!mInitialized || !mSwrInitialized || !mSwrContext) {
        return nullptr;
    }
    
    // 如果想要的采样数和原始采样数相同，直接返回拷贝
    if (wantedSamples == inputFrame.mNbSamples) {
        auto outputFrame = std::make_unique<AudioFrame>();
        outputFrame->mPts = inputFrame.mPts;
        outputFrame->mDuration = inputFrame.mDuration;
        outputFrame->mSampleRate = inputFrame.mSampleRate;
        outputFrame->mChannels = inputFrame.mChannels;
        outputFrame->mNbSamples = inputFrame.mNbSamples;
        outputFrame->mData = inputFrame.mData;
        return outputFrame;
    }
    
    // 计算需要补偿的采样数差值
    int sampleDelta = wantedSamples - inputFrame.mNbSamples;
    
    // 使用 swr_set_compensation 进行软补偿
    // 参数：sample_delta（需要补偿的采样数），compensation_distance（补偿距离，通常设为输入采样数）
    if (swr_set_compensation(mSwrContext, sampleDelta, inputFrame.mNbSamples) < 0) {
        std::cerr << "Failed to set compensation" << std::endl;
        return nullptr;
    }
    
    // 准备输入数据
    const uint8_t* inputData[2] = {nullptr};
    inputData[0] = inputFrame.mData.data();
    
    // 计算最大可能的输出采样数
    int maxOutputSamples = swr_get_out_samples(mSwrContext, inputFrame.mNbSamples);
    if (maxOutputSamples <= 0) {
        maxOutputSamples = wantedSamples + 64; // 添加一些缓冲
    }
    
    // 准备输出缓冲区
    size_t outputDataSize = maxOutputSamples * inputFrame.mChannels * sizeof(int16_t);
    mOutputBuffer.resize(outputDataSize / sizeof(int16_t));
    
    uint8_t* outputData[2] = {nullptr};
    outputData[0] = reinterpret_cast<uint8_t*>(mOutputBuffer.data());
    
    // 执行重采样
    int outputSamples = swr_convert(mSwrContext, outputData, maxOutputSamples, inputData, inputFrame.mNbSamples);
    
    if (outputSamples <= 0) {
        std::cerr << "swr_convert failed, returned: " << outputSamples << std::endl;
        return nullptr;
    }
    
    // 创建输出帧
    auto outputFrame = std::make_unique<AudioFrame>();
    outputFrame->mPts = inputFrame.mPts;
    outputFrame->mDuration = inputFrame.mDuration;
    outputFrame->mSampleRate = inputFrame.mSampleRate;
    outputFrame->mChannels = inputFrame.mChannels;
    outputFrame->mNbSamples = outputSamples;
    
    size_t actualDataSize = outputSamples * inputFrame.mChannels * sizeof(int16_t);
    outputFrame->mData.resize(actualDataSize);
    
    std::memcpy(outputFrame->mData.data(), mOutputBuffer.data(), actualDataSize);
    
    std::cerr << "[resampleToWantedSamples] input: " << inputFrame.mNbSamples 
              << ", wanted: " << wantedSamples 
              << ", output: " << outputSamples 
              << ", delta: " << sampleDelta << std::endl;
    
    return outputFrame;
}

void AudioProcessor::flush() {
    if (mSonicStream) {
        sonicFlushStream(mSonicStream);
    }
}

void AudioProcessor::reset() {
    if (mInitialized) {
        flush();
        // 重新初始化
        initialize(mSampleRate, mChannels);
        setPlaybackRate(mCurrentRate);
    }
}

void AudioProcessor::floatToShort(const float* input, short* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        float sample = input[i];
        // 限制范围到 [-1.0, 1.0]
        sample = std::max(-1.0f, std::min(1.0f, sample));
        // 转换到 short 范围
        output[i] = static_cast<short>(sample * 32767.0f);
    }
}

void AudioProcessor::shortToFloat(const short* input, float* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        output[i] = static_cast<float>(input[i]) / 32767.0f;
    }
}

}  // namespace yffplayer
