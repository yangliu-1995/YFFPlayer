#pragma once

#include <cstdint>
#include <string>

#include "PlayerTypes.h"

extern "C" {
struct AVCodecParameters;
}

namespace yffplayer {
struct MediaInfo {
    MediaType type{MediaType::UNKNOWN};  // 媒体类型
    AVCodecParameters *audioCodecParam{nullptr};                 // 音频编解码器ID
    AVCodecParameters *videoCodecParam{nullptr};                 // 编解码器ID
    int64_t durationMs{0};               // 媒体总时长（毫秒）
    bool hasVideo{false};                // 是否有视频流
    bool hasAudio{false};                // 是否有音频流

    int videoWidth{0};       // 视频宽度
    int videoHeight{0};      // 视频高度
    int audiochannels{0};    // 音频通道数
    int audioSampleRate{0};  // 音频采样率
};
}  // namespace yffplayer
