#include "AudioResampleContext.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace {
constexpr AVSampleFormat kTargetFormat = AV_SAMPLE_FMT_S16;
constexpr int kTargetSampleRate = 44100;
constexpr int kTargetNbChannels = 2;
}

namespace yffplayer {
AudioResampleContext::AudioResampleContext(int sampleRate, int format, int nbChannels): mInSampleRate(sampleRate), mInFormat(format), mInNbChannels(nbChannels){
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kTargetNbChannels);

    AVChannelLayout inLayout;
    av_channel_layout_default(&inLayout, nbChannels);

    mSwrContext = swr_alloc();
    if (!mSwrContext) {
        return;
    }
    if (swr_alloc_set_opts2(&mSwrContext, &outLayout, kTargetFormat, kTargetSampleRate, &inLayout, static_cast<AVSampleFormat>(format), sampleRate, 0, nullptr) < 0) {
        swr_free(&mSwrContext);
        return;
    }
    if (swr_init(mSwrContext) < 0) {
        swr_free(&mSwrContext);
        return;
    }
    av_channel_layout_uninit(&outLayout);
    av_channel_layout_uninit(&inLayout);
}

AudioResampleContext::~AudioResampleContext() {
    if (mSwrContext) {
        swr_free(&mSwrContext);
        mSwrContext = nullptr;
    }
}

SwrContext* AudioResampleContext::getSwrContext() const {
    return mSwrContext;
}
} // namespace yffplayer
