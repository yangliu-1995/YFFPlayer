#include "AudioFrameProcessor.h"

#include <algorithm>
#include <cstring>
#include <iostream>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace {
constexpr AVSampleFormat kTargetFormat = AV_SAMPLE_FMT_S16;
constexpr int kTargetSampleRate = 44100;
constexpr int kTargetNbChannles = 2;
constexpr double kMaxDelay = 0.05; // 50ms
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
    int outSampleRate = 44100; // 固定输出采样率 44100 Hz
    int outChannels = 2; // 固定输出为 2 通道（立体声）
    AVSampleFormat sampleFmt = AV_SAMPLE_FMT_S16; // 固定输出为 16-bit PCM

    SwrContext* swrContext = mResampleContext->getSwrContext();
    if (!swrContext) {
        return nullptr;
    }

    // 检查输入帧的通道数
    int inChannels = frame.ch_layout.nb_channels;

    // 判断是否需要直接拷贝（样本数相同且格式一致）
    if (inSampleRate == outSampleRate && sampleFmt == frame.format && inChannels == outChannels) {
        // 直接拷贝原始数据
        int bufferSize = av_samples_get_buffer_size(
            nullptr,
            outChannels,
            frame.nb_samples,
            sampleFmt,
            0
        );
        if (bufferSize < 0) {
            return nullptr;
        }

        std::vector<uint8_t> audioBuffer(bufferSize);
        memcpy(audioBuffer.data(), frame.data[0], bufferSize);

        // 计算 duration
        int64_t duration = av_rescale_q(frame.nb_samples, {1, outSampleRate}, {1, AV_TIME_BASE}) / 1000.f;

        return std::make_unique<AudioFrame>(
            frame.pts,
            duration,
            outSampleRate,
            outChannels,
            frame.nb_samples,
            std::move(audioBuffer)
        );
    }

    int outSamples = (int)av_rescale_rnd(swr_get_delay(swrContext, mSampleRate) + frame.nb_samples, outSampleRate, mSampleRate, AV_ROUND_UP);

    int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    int outBufferSize = outSamples * outChannels * bytesPerSample;
    std::vector<uint8_t> buffer(outBufferSize);
    uint8_t* out[] = {buffer.data()};

    int samples = swr_convert(swrContext, out, outSamples, (const uint8_t**)frame.data, frame.nb_samples);
    if (samples < 0) {
        std::cerr << "swr_convert failed: " << samples << std::endl;
    }

    int64_t pts = (frame.pts == AV_NOPTS_VALUE) ? frame.best_effort_timestamp : frame.pts;

    int64_t duration = av_rescale_q(outSamples, {1, outSampleRate}, {1, AV_TIME_BASE}) / 1000.f;

    return std::make_unique<AudioFrame>(
        pts,
        duration,
        outSampleRate,
        outChannels,
        outSamples,
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
