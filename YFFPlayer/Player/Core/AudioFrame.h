#pragma once

#include <memory>

namespace yffplayer {
struct AudioFrame {
    int64_t pts;       // PTS timestamp
    int64_t duration;  // Duration
    int64_t size;      // Data size
    uint8_t* data;     // Data pointer
    int channels;      // Number of channels
    int sampleRate;    // Sample rate
    int bitDepth;      // Bit depth
};
}  // namespace yffplayer
