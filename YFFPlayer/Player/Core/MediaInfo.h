#pragma once

#include <cstdint>
#include <string>

#include "PlayerTypes.h"

extern "C" {
struct AVCodecParameters;
}

namespace yffplayer {
struct MediaInfo {
    MediaType type{MediaType::UNKNOWN};           // Media type
    AVCodecParameters *audioCodecParam{nullptr};  // Audio codec parameters
    AVCodecParameters *videoCodecParam{nullptr};  // Video codec parameters
    int64_t durationMs{0};                        // Total media duration (milliseconds)
    bool hasVideo{false};                         // Whether it has video stream
    bool hasAudio{false};                         // Whether it has audio stream

    int videoWidth{0};       // Video width
    int videoHeight{0};      // Video height
    int audiochannels{0};    // Audio channels
    int audioSampleRate{0};  // Audio sample rate
};
}  // namespace yffplayer
