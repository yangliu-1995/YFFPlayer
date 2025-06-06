#include "AudioResampleContext.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace {
constexpr AVSampleFormat kTargetFormat = AV_SAMPLE_FMT_S16;
constexpr int kTargetSampleRate = 48000;
constexpr int kTargetNbChannels = 2;
}  // namespace

namespace yffplayer {
AudioResampleContext::AudioResampleContext(int sampleRate, int format, int nbChannels)
    : inSampleRate_(sampleRate), inFormat_(format), inNbChannels_(nbChannels) {
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kTargetNbChannels);

    AVChannelLayout inLayout;
    av_channel_layout_default(&inLayout, nbChannels);

    swrContext_ = swr_alloc();
    if (!swrContext_) {
        return;
    }
    if (swr_alloc_set_opts2(&swrContext_, &outLayout, kTargetFormat, kTargetSampleRate, &inLayout,
                            static_cast<AVSampleFormat>(format), sampleRate, 0, nullptr) < 0) {
        swr_free(&swrContext_);
        return;
    }
    if (swr_init(swrContext_) < 0) {
        swr_free(&swrContext_);
        return;
    }
    av_channel_layout_uninit(&outLayout);
    av_channel_layout_uninit(&inLayout);
}

AudioResampleContext::~AudioResampleContext() {
    if (swrContext_) {
        swr_free(&swrContext_);
        swrContext_ = nullptr;
    }
}

SwrContext* AudioResampleContext::getSwrContext() const { return swrContext_; }

int AudioResampleContext::getOutSampleRate() const { return kTargetSampleRate; }

int AudioResampleContext::getOutFormat() const { return kTargetFormat; }

int AudioResampleContext::getOutNbChannels() const { return kTargetNbChannels; }
}  // namespace yffplayer
