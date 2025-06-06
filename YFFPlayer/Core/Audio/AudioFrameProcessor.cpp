#include "AudioFrameProcessor.h"

#include <algorithm>
#include <cstring>

#include "Log.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

namespace yffplayer {

AudioFrameProcessor::AudioFrameProcessor()
    : sonicStream_(nullptr),
      sampleRate_(0),
      channels_(0),
      currentRate_(1.0f),
      initialized_(false) {}

AudioFrameProcessor::~AudioFrameProcessor() {
    if (sonicStream_) {
        sonicDestroyStream(sonicStream_);
        sonicStream_ = nullptr;
    }
}

bool AudioFrameProcessor::initialize(int sampleRate, int channels, int format) {
    if (sonicStream_) {
        sonicDestroyStream(sonicStream_);
    }

    sampleRate_ = sampleRate;
    channels_ = channels;
    format_ = format;

    sonicStream_ = sonicCreateStream(sampleRate, channels);
    if (!sonicStream_) {
        LogInfo << "Failed to create Sonic stream";
        return false;
    }

    // 设置默认参数
    sonicSetSpeed(sonicStream_, currentRate_);
    sonicSetPitch(sonicStream_, 1.0f);  // 保持音调不变

    resampleContext_ = std::make_unique<AudioResampleContext>(sampleRate, format, channels);

    initialized_ = true;
    return true;
}

void AudioFrameProcessor::setPlaybackRate(float rate) {
    if (!initialized_ || !sonicStream_) {
        return;
    }

    currentRate_ = rate;
    sonicSetSpeed(sonicStream_, rate);
}

void AudioFrameProcessor::setPitch(float pitch) {
    if (!initialized_ || !sonicStream_) {
        return;
    }
    sonicSetPitch(sonicStream_, pitch);
}

std::unique_ptr<AudioFrame> AudioFrameProcessor::processAudioFrame(
    const std::shared_ptr<FrameHandle> frameHandle, double delay) {
    AVFrame* frame = frameHandle->getFrame();
    SwrContext* swrContext = resampleContext_->getSwrContext();
    if (!swrContext) {
        return nullptr;
    }
    auto audioFrame = reSampleAVFrame(*frame);
    if (std::abs(currentRate_ - 1.0f) < 0.01f) {
        return audioFrame;
    }
    int inputSamples = audioFrame->nbSamples_;
    short* inputData = (short*)audioFrame->data_.data();
    __unused int samplesWritten = sonicWriteShortToStream(sonicStream_, inputData, inputSamples);
    int availableSamples = sonicSamplesAvailable(sonicStream_);
    if (availableSamples <= 0) {
        return nullptr;
    }
    int totalOutputSamples = availableSamples * audioFrame->channels_;
    outputBuffer_.resize(totalOutputSamples);
    int samplesRead =
        sonicReadShortFromStream(sonicStream_, outputBuffer_.data(), availableSamples);

    if (samplesRead <= 0) {
        return nullptr;
    }
    auto outputFrame = std::make_unique<AudioFrame>();
    outputFrame->pts_ = audioFrame->pts_;
    outputFrame->duration_ = static_cast<int64_t>(audioFrame->duration_ / currentRate_);
    outputFrame->sampleRate_ = audioFrame->sampleRate_;
    outputFrame->channels_ = audioFrame->channels_;
    outputFrame->nbSamples_ = samplesRead;

    size_t dataSize = samplesRead * audioFrame->channels_ * sizeof(int16_t);
    outputFrame->data_.resize(dataSize);
    std::memcpy(outputFrame->data_.data(), outputBuffer_.data(), dataSize);
    return outputFrame;
}

std::unique_ptr<AudioFrame> AudioFrameProcessor::reSampleAVFrame(const AVFrame& frame) {
    int inSampleRate = frame.sample_rate;
    int outSampleRate = resampleContext_->getOutSampleRate();
    int outChannels = resampleContext_->getOutNbChannels();
    AVSampleFormat outSampleFmt = static_cast<AVSampleFormat>(resampleContext_->getOutFormat());

    SwrContext* swrContext = resampleContext_->getSwrContext();
    if (!swrContext) {
        return nullptr;
    }

    int inChannels = frame.ch_layout.nb_channels;

    // 直接拷贝无需重采样
    if (inSampleRate == outSampleRate && outSampleFmt == frame.format &&
        inChannels == outChannels) {
        int bufferSize =
            av_samples_get_buffer_size(nullptr, outChannels, frame.nb_samples, outSampleFmt, 0);
        if (bufferSize < 0) {
            return nullptr;
        }

        std::vector<uint8_t> audioBuffer(bufferSize);
        memcpy(audioBuffer.data(), frame.data[0], bufferSize);

        int64_t duration =
            av_rescale_q(frame.nb_samples, {1, outSampleRate}, {1, AV_TIME_BASE}) / 1000;

        return std::make_unique<AudioFrame>(frame.pts, duration, outSampleRate, outChannels,
                                            frame.nb_samples, std::move(audioBuffer));
    }

    // 重采样
    int maxOutSamples =
        (int)av_rescale_rnd(swr_get_delay(swrContext, inSampleRate) + frame.nb_samples,
                            outSampleRate, inSampleRate, AV_ROUND_UP);

    int bytesPerSample = av_get_bytes_per_sample(outSampleFmt);
    int outBufferSize = maxOutSamples * outChannels * bytesPerSample;
    std::vector<uint8_t> buffer(outBufferSize);
    memset(buffer.data(), 0, outBufferSize);  // 清除未写部分残留值

    uint8_t* out[] = {buffer.data()};
    int samples =
        swr_convert(swrContext, out, maxOutSamples, (const uint8_t**)frame.data, frame.nb_samples);
    if (samples < 0) {
        LogInfo << "swr_convert failed: " << samples;
        return nullptr;
    }

    int validSize = samples * outChannels * bytesPerSample;
    buffer.resize(validSize);  // 精准修剪为实际数据长度

    int64_t pts = (frame.pts == AV_NOPTS_VALUE) ? frame.best_effort_timestamp : frame.pts;
    int64_t duration = av_rescale_q(samples, {1, outSampleRate}, {1, AV_TIME_BASE}) / 1000;

    return std::make_unique<AudioFrame>(pts, duration, outSampleRate, outChannels, samples,
                                        std::move(buffer));
}

void AudioFrameProcessor::flush() {
    if (sonicStream_) {
        sonicFlushStream(sonicStream_);
    }
}

void AudioFrameProcessor::reset() {
    if (initialized_) {
        flush();
        // 重新初始化
        initialize(sampleRate_, channels_, format_);
        setPlaybackRate(currentRate_);
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
