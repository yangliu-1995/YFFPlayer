#include "SonicAudioProcessor.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace yffplayer {

SonicAudioProcessor::SonicAudioProcessor()
    : mSonicStream(nullptr)
    , mSampleRate(0)
    , mChannels(0)
    , mCurrentRate(1.0f)
    , mInitialized(false) {
}

SonicAudioProcessor::~SonicAudioProcessor() {
    if (mSonicStream) {
        sonicDestroyStream(mSonicStream);
        mSonicStream = nullptr;
    }
}

bool SonicAudioProcessor::initialize(int sampleRate, int channels) {
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
    
    mInitialized = true;
    return true;
}

void SonicAudioProcessor::setPlaybackRate(float rate) {
    if (!mInitialized || !mSonicStream) {
        return;
    }
    
    mCurrentRate = rate;
    sonicSetSpeed(mSonicStream, rate);
}

void SonicAudioProcessor::setPitch(float pitch) {
    if (!mInitialized || !mSonicStream) {
        return;
    }
    
    sonicSetPitch(mSonicStream, pitch);
}

std::unique_ptr<AudioFrame> SonicAudioProcessor::processAudioFrame(const AudioFrame& inputFrame) {
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
    int samplesWritten = sonicWriteShortToStream(mSonicStream, 
                                                inputData,
                                                inputSamples);

    int availableSamples = sonicSamplesAvailable(mSonicStream);
    if (availableSamples <= 0) {
        return nullptr;
    }

    int totalOutputSamples = availableSamples * inputFrame.mChannels;
    mOutputBuffer.resize(totalOutputSamples);
    
    int samplesRead = sonicReadShortFromStream(mSonicStream, 
                                              mOutputBuffer.data(), 
                                              availableSamples);
    
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

void SonicAudioProcessor::flush() {
    if (mSonicStream) {
        sonicFlushStream(mSonicStream);
    }
}

void SonicAudioProcessor::reset() {
    if (mInitialized) {
        flush();
        // 重新初始化
        initialize(mSampleRate, mChannels);
        setPlaybackRate(mCurrentRate);
    }
}

void SonicAudioProcessor::floatToShort(const float* input, short* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        float sample = input[i];
        // 限制范围到 [-1.0, 1.0]
        sample = std::max(-1.0f, std::min(1.0f, sample));
        // 转换到 short 范围
        output[i] = static_cast<short>(sample * 32767.0f);
    }
}

void SonicAudioProcessor::shortToFloat(const short* input, float* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        output[i] = static_cast<float>(input[i]) / 32767.0f;
    }
}

}  // namespace yffplayer
