#pragma once
#include <memory>
#include "AudioOutputFrameProvider.h"

namespace yffplayer {
class AudioFrame;

class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    virtual bool initialize(int sampleRate, int channels, int frameBytes, std::shared_ptr<AudioOutputFrameProvider> frameProvider) = 0;
    virtual void start() = 0;
    virtual void feedAudioFrame(const AudioFrame& frame) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
};
} // namespace yffplayer
