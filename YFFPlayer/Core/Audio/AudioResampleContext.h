#pragma once

extern "C" {
#include <libswresample/swresample.h>
}

namespace yffplayer {
class AudioResampleContext {
public:
    explicit AudioResampleContext(int sampleRate, int format, int nbChannels);
    ~AudioResampleContext();
    SwrContext* getSwrContext() const;

private:
    SwrContext* mSwrContext{nullptr};
    int mInSampleRate{0};
    int mInFormat{0};
    int mInNbChannels{0};
};
} // namespace yffplayer
