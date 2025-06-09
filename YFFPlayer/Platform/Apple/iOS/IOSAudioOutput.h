#pragma once

#include <AudioToolbox/AudioToolbox.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

#include "AudioOutput.h"

class IOSAudioOutput : public yffplayer::AudioOutput {
public:
    IOSAudioOutput();
    ~IOSAudioOutput();

    bool init(int sampleRate, int channels) override;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void flush() override;
    void setVolume(float volume) override;
    void setMute(bool mute) override;
    bool enqueueAudioFrame(const yffplayer::AudioFrame& frame) override;
    void setPlaybackCallback(yffplayer::AudioPlaybackCallback callback) override;

private:
    static void AudioQueueCallback(void* userData, AudioQueueRef inAQ,
                                   AudioQueueBufferRef inBuffer);
    void handleBuffer(AudioQueueBufferRef inBuffer);

    bool running_ = false;
    bool paused_ = false;

    int sampleRate_ = 0;
    int channels_ = 0;
    UInt32 frameBytes_ = 0;

    float volume_ = 1.0f;
    bool mute_ = false;

    AudioQueueRef audioQueue_ = nullptr;
    static constexpr int kNumBuffers = 3;
    AudioQueueBufferRef buffers_[kNumBuffers];

    std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<yffplayer::AudioFrame> frameQueue_;
    const size_t maxQueueSize_ = 4;
    yffplayer::AudioPlaybackCallback playbackCallback_;
};
