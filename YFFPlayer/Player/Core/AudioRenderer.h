#pragma once

#include <cstdint>
#include <memory>

#include "RendererCallback.h"

namespace yffplayer {
struct AudioFrame;

class AudioRenderer {
   public:
    virtual ~AudioRenderer() = default;

    // Initialize renderer
    virtual bool init(int sampleRate, int channels, int bitsPerSample,
                      std::shared_ptr<RendererCallback> callback) = 0;

    // Play audio data
    virtual bool play(const AudioFrame& frame) = 0;

    // Pause audio playback
    virtual void pause() = 0;

    // Resume audio playback
    virtual void resume() = 0;

    // Stop audio playback
    virtual void stop() = 0;

    // Set volume
    virtual void setVolume(float volume) = 0;

    // Get volume
    virtual float getVolume() const = 0;

    // Set mute
    virtual void setMute(bool mute) = 0;

    // Get mute status
    virtual bool isMuted() const = 0;

    // Release resources
    virtual void release() = 0;
};
}  // namespace yffplayer
