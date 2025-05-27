#pragma once

#include "AudioOutput.h"
#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <condition_variable>
#include <deque>

class IOSAudioOutput : public yffplayer::AudioOutput {
public:
    IOSAudioOutput();
    ~IOSAudioOutput();

    bool init(int sampleRate, int channels) override;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void setVolume(float volume) override;
    void setMute(bool mute) override;
    bool enqueueAudioFrame(const yffplayer::AudioFrame& frame) override;

private:
    static void AudioQueueCallback(void* userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);
    void handleBuffer(AudioQueueBufferRef inBuffer);

    bool mRunning = false;
    bool mPaused = false;

    int mSampleRate = 0;
    int mChannels = 0;
    UInt32 mFrameBytes = 0;

    float mVolume = 1.0f;
    bool mMute = false;

    AudioQueueRef mAudioQueue = nullptr;
    static constexpr int kNumBuffers = 3;
    AudioQueueBufferRef mBuffers[kNumBuffers];

    std::mutex mMutex;
    std::condition_variable mCond;
    std::deque<yffplayer::AudioFrame> mFrameQueue;
    const size_t mMaxQueueSize = 20;
};
