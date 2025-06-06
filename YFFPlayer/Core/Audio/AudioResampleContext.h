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
    int getOutSampleRate() const;
    int getOutFormat() const;
    int getOutNbChannels() const;

private:
    SwrContext* swrContext_{nullptr};
    int inSampleRate_{0};
    int inFormat_{0};
    int inNbChannels_{0};
};
}  // namespace yffplayer
