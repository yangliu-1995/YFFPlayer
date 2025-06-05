#include "AudioFrameProcessor.h"

#include <algorithm>
#include <cstring>
#include <iostream>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace yffplayer {

AudioFrameProcessor::AudioFrameProcessor()
    : mSonicStream(nullptr),
      mSampleRate(0),
      mChannels(0),
      mCurrentRate(1.0f),
      mInitialized(false) {}

AudioFrameProcessor::~AudioFrameProcessor() {
    if (mSonicStream) {
        sonicDestroyStream(mSonicStream);
        mSonicStream = nullptr;
    }
}

bool AudioFrameProcessor::initialize(int sampleRate, int channels, int format) {
    if (mSonicStream) {
        sonicDestroyStream(mSonicStream);
    }

    mSampleRate = sampleRate;
    mChannels = channels;
    mFormat = format;

    mSonicStream = sonicCreateStream(sampleRate, channels);
    if (!mSonicStream) {
        std::cerr << "Failed to create Sonic stream" << std::endl;
        return false;
    }

    // 设置默认参数
    sonicSetSpeed(mSonicStream, mCurrentRate);
    sonicSetPitch(mSonicStream, 1.0f);  // 保持音调不变

    mResampleContext = std::make_unique<AudioResampleContext>(sampleRate, format, channels);

    mInitialized = true;
    return true;
}

void AudioFrameProcessor::setPlaybackRate(float rate) {
    if (!mInitialized || !mSonicStream) {
        return;
    }

    mCurrentRate = rate;
    sonicSetSpeed(mSonicStream, rate);
}

void AudioFrameProcessor::setPitch(float pitch) {
    if (!mInitialized || !mSonicStream) {
        return;
    }
    sonicSetPitch(mSonicStream, pitch);
}

std::unique_ptr<AudioFrame> AudioFrameProcessor::processAudioFrame(const std::shared_ptr<FrameHandle> frameHandle, double delay) {
    AVFrame *frame = frameHandle->getFrame();
    SwrContext* swrContext = mResampleContext->getSwrContext();
    if (!swrContext) {
        return nullptr;
    }
    auto audioFrame = reSampleAVFrame(*frame);
    if (std::abs(mCurrentRate - 1.0f) < 0.01f) {
        return audioFrame;
    }
    int inputSamples = audioFrame->mNbSamples;
    short* inputData = (short*)audioFrame->mData.data();
    __unused int samplesWritten = sonicWriteShortToStream(mSonicStream, inputData, inputSamples);
    int availableSamples = sonicSamplesAvailable(mSonicStream);
    if (availableSamples <= 0) {
        return nullptr;
    }
    int totalOutputSamples = availableSamples * audioFrame->mChannels;
        mOutputBuffer.resize(totalOutputSamples);
    int samplesRead = sonicReadShortFromStream(mSonicStream, mOutputBuffer.data(), availableSamples);

    if (samplesRead <= 0) {
        return nullptr;
    }
    auto outputFrame = std::make_unique<AudioFrame>();
    outputFrame->mPts = audioFrame->mPts;
    outputFrame->mDuration = static_cast<int64_t>(audioFrame->mDuration / mCurrentRate);
    outputFrame->mSampleRate = audioFrame->mSampleRate;
    outputFrame->mChannels = audioFrame->mChannels;
    outputFrame->mNbSamples = samplesRead;

    size_t dataSize = samplesRead * audioFrame->mChannels * sizeof(int16_t);
    outputFrame->mData.resize(dataSize);
    std::memcpy(outputFrame->mData.data(), mOutputBuffer.data(), dataSize);
    return outputFrame;
}

std::unique_ptr<AudioFrame> AudioFrameProcessor::reSampleAVFrame(const AVFrame& frame) {
    int inSampleRate = frame.sample_rate;
    int outSampleRate = mResampleContext->getOutSampleRate();
    int outChannels = mResampleContext->getOutNbChannels();
    AVSampleFormat outSampleFmt = static_cast<AVSampleFormat>(mResampleContext->getOutFormat());

    SwrContext* swrContext = mResampleContext->getSwrContext();
    if (!swrContext) {
        return nullptr;
    }

    int inChannels = frame.ch_layout.nb_channels;

    // 直接拷贝无需重采样
    if (inSampleRate == outSampleRate && outSampleFmt == frame.format && inChannels == outChannels) {
        int bufferSize = av_samples_get_buffer_size(nullptr, outChannels, frame.nb_samples, outSampleFmt, 0);
        if (bufferSize < 0) {
            return nullptr;
        }

        std::vector<uint8_t> audioBuffer(bufferSize);
        memcpy(audioBuffer.data(), frame.data[0], bufferSize);

        int64_t duration = av_rescale_q(frame.nb_samples, {1, outSampleRate}, {1, AV_TIME_BASE}) / 1000;

        return std::make_unique<AudioFrame>(
            frame.pts,
            duration,
            outSampleRate,
            outChannels,
            frame.nb_samples,
            std::move(audioBuffer)
        );
    }

    // 重采样
    int maxOutSamples = (int)av_rescale_rnd(
        swr_get_delay(swrContext, inSampleRate) + frame.nb_samples,
        outSampleRate,
        inSampleRate,
        AV_ROUND_UP
    );

    int bytesPerSample = av_get_bytes_per_sample(outSampleFmt);
    int outBufferSize = maxOutSamples * outChannels * bytesPerSample;
    std::vector<uint8_t> buffer(outBufferSize);
    memset(buffer.data(), 0, outBufferSize); // 清除未写部分残留值

    uint8_t* out[] = {buffer.data()};
    int samples = swr_convert(swrContext, out, maxOutSamples, (const uint8_t**)frame.data, frame.nb_samples);
    if (samples < 0) {
        std::cerr << "swr_convert failed: " << samples << std::endl;
        return nullptr;
    }

    int validSize = samples * outChannels * bytesPerSample;
    buffer.resize(validSize); // 精准修剪为实际数据长度

    int64_t pts = (frame.pts == AV_NOPTS_VALUE) ? frame.best_effort_timestamp : frame.pts;
    int64_t duration = av_rescale_q(samples, {1, outSampleRate}, {1, AV_TIME_BASE}) / 1000;

    return std::make_unique<AudioFrame>(
        pts,
        duration,
        outSampleRate,
        outChannels,
        samples,
        std::move(buffer)
    );
}

void AudioFrameProcessor::flush() {
    if (mSonicStream) {
        sonicFlushStream(mSonicStream);
    }
}

void AudioFrameProcessor::reset() {
    if (mInitialized) {
        flush();
        // 重新初始化
        initialize(mSampleRate, mChannels, mFormat);
        setPlaybackRate(mCurrentRate);
    }
}

void AudioFrameProcessor::floatToShort(const float* input, short* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        float sample = input[i];
        // 限制范围到 [-1.0, 1.0]
        sample = std::max(-1.0f, std::min(1.0f, sample));
        // 转换到 short 范围
        output[i] = static_cast<short>(sample * 32767.0f);
    }
}

void AudioFrameProcessor::shortToFloat(const short* input, float* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        output[i] = static_cast<float>(input[i]) / 32767.0f;
    }
}

}  // namespace yffplayer
