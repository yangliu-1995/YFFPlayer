#pragma once

#include "AudioRenderer.h"
#include "AudioFrame.h"

#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <queue>
#include <vector>

namespace yffplayer {

class AppleAudioUnitRenderer : public AudioRenderer {
public:
    AppleAudioUnitRenderer();
    ~AppleAudioUnitRenderer() override;

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
    // AudioUnit render callback
    static OSStatus RenderCallback(void* inRefCon,
                                  AudioUnitRenderActionFlags* ioActionFlags,
                                  const AudioTimeStamp* inTimeStamp,
                                  UInt32 inBusNumber,
                                  UInt32 inNumberFrames,
                                  AudioBufferList* ioData);
    
    // Process audio data in the callback
    OSStatus handleRenderCallback(AudioUnitRenderActionFlags* ioActionFlags,
                                 const AudioTimeStamp* inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList* ioData);
    
    // Setup audio unit
    bool setupAudioUnit();
    
    // Setup audio format
    void setupAudioFormat(AudioStreamBasicDescription& format, int sampleRate, 
                         int channels, int bitsPerSample);

    AudioUnit audioUnit_;
    std::shared_ptr<RendererCallback> callback_;
    mutable std::mutex mutex_;
    std::queue<AudioFrame> frameQueue_; // Store audio frames to be played
    std::vector<uint8_t> renderBuffer_; // Buffer for audio rendering
    
    bool isPlaying_ = false;
    bool isMuted_ = false;
    float volume_ = 1.0f;
    int sampleRate_ = 0;
    int channels_ = 0;
    int bitsPerSample_ = 0;
    
    // Constants for buffer management
    static constexpr int kMaxBufferSize = 192000; // Max buffer size (2s of 48kHz stereo 16-bit audio)
};

} // namespace yffplayer