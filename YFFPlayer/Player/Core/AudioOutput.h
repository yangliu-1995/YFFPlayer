#pragma once
#include <memory>

#include "AudioFrame.h"

namespace yffplayer {
class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    virtual bool init(int sampleRate, int channels) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void flush() = 0;
    virtual void setVolume(float volume) = 0;  // 0.0 ~ 1.0
    virtual void setMute(bool mute) = 0;
    virtual bool enqueueAudioFrame(const AudioFrame& frame) = 0;
};
}  // namespace yffplayer
