#pragma once

#include "AudioRenderer.h"
#include "AudioFrame.h"

#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <queue>

namespace yffplayer {

struct AudioFrame;

class IOSAudioRenderer : public AudioRenderer {
public:
    IOSAudioRenderer();
    ~IOSAudioRenderer() override;

    // Initialize renderer
    bool init(int sampleRate, int channels, int bitsPerSample,
              std::shared_ptr<RendererCallback> callback) override;

    // Play audio data
    bool play(const AudioFrame& frame) override;

    // Pause audio playback
    void pause() override;

    // Resume audio playback
    void resume() override;

    // Stop audio playback
    void stop() override;

    // Set volume
    void setVolume(float volume) override;

    // Get volume
    float getVolume() const override;

    // Set mute
    void setMute(bool mute) override;

    // Get mute state
    bool isMuted() const override;

    // Release resources
    void release() override;

private:
    // AudioQueue callback function
    static void AudioQueueOutputCallback(void* inUserData,
                                        AudioQueueRef inAQ,
                                        AudioQueueBufferRef inBuffer);

    // Handle buffer completion
    void handleBufferCompleted(AudioQueueBufferRef buffer);

    // Fill audio buffer
    bool enqueueAudioFrame(const AudioFrame& frame);

    AudioQueueRef audioQueue_ = nullptr;
    std::shared_ptr<RendererCallback> callback_;
    mutable std::mutex mutex_;
    std::queue<AudioFrame> frameQueue_; // Store audio frames to be played
    bool isPlaying_ = false;
    bool isMuted_ = false;
    float volume_ = 1.0f;
    int sampleRate_ = 0;
    int channels_ = 0;
    int bitsPerSample_ = 0;
    bool isStarted_ = false;

    static constexpr int kNumBuffers = 3; // Number of AudioQueue buffers
    AudioQueueBufferRef buffers_[kNumBuffers];
};

} // namespace yffplayer
