#pragma once

#include <string>

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
struct AVCodecParameters;
}

namespace yffplayer {
struct MediaInfo {
    AVCodecParameters *mAudioCodecParameters{nullptr};
    AVRational mAudioTimeBase{0, 1};
    AVCodecParameters *mVideoCodecParameters{nullptr};
    AVRational mVideoTimeBase{0, 1};
    int mVideoFrameRate{30};
    int64_t mDurationMs{0};
    bool mHasVideo{false};
    bool mHasAudio{false};

    int mVideoWidth{0};
    int mVideoHeight{0};
    int mAudioChannels{0};
    int mAudioSampleRate{0};

    ~MediaInfo() {
        avcodec_parameters_free(&mAudioCodecParameters);
        avcodec_parameters_free(&mVideoCodecParameters);
    };
};
}
