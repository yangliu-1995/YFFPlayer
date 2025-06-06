#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
struct AVCodecParameters;
}

namespace yffplayer {
struct MediaInfo {
    AVCodecParameters *audioCodecParameters_{nullptr};
    AVRational audioTimeBase_{0, 1};
    AVCodecParameters *videoCodecParameters_{nullptr};
    AVRational videoTimeBase_{0, 1};
    int videoFrameRate_{30};
    int64_t durationMs_{0};
    bool hasVideo_{false};
    bool hasAudio_{false};
    bool isLiveStream_{false};
    bool isTsDiscont_{false};

    int videoWidth_{0};
    int videoHeight_{0};
    int audioChannels_{0};
    int audioSampleRate_{0};

    ~MediaInfo() {
        avcodec_parameters_free(&audioCodecParameters_);
        avcodec_parameters_free(&videoCodecParameters_);
    };
};
}  // namespace yffplayer
