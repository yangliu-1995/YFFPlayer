#pragma once

#include <string>

namespace yffplayer {

static constexpr int kAudioTargetSampleRate = 48000;
static constexpr int kAudioTargetChannels = 2;
static constexpr int kAudioTargetBitDepth = 16;

enum class MediaType {
    UNKNOWN,
    VIDEO,
    AUDIO,
    AUDIO_VIDEO,
};

enum class PlayerState {
    IDLE,
    INITIALIZED,
    PREPARING,
    PREPARED,
    STARTED,
    PAUSED,
    STOPPED,
    COMPLETED,
    ERROR,
};

enum class ErrorCode {
    SUCCESS = 0,
    UNKNOWN_ERROR = -1,
    FILE_NOT_FOUND = -100,
    OPEN_FILE_FAILED = -101,
    STREAM_NOT_FOUND = -102,
    CODEC_NOT_FOUND = -103,
    DECODER_INIT_FAILED = -104,
    DEMUXER_OPEN_FAILED = -105,
    DEMUXER_FIND_STREAM_FAILED = -106,
    DEMUXER_READ_FAILED = -107,
    DEMUXER_EXCEPTION = -108,
    NETWORK_ERROR = -200,
};

struct Error {
    ErrorCode code;
    std::string message;
};

enum class StreamType {
    AUDIO,
    VIDEO,
    UNKNOWN,
};

struct Packet {
    StreamType type{StreamType::UNKNOWN};
    uint8_t* data{nullptr};
    int64_t pts{0};
    int64_t dts{0};
    int size{0};
    int streamIndex{-1};
    bool isKeyframe{false};
};

enum class DemuxerState {
    IDLE,
    INITIALIZED,
    RUNNING,
    SEEKING,
    PAUSED,
    STOPPED,
    ERROR
};

}  // namespace yffplayer
