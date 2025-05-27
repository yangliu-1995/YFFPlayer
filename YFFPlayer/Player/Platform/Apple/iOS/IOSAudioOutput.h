#pragma once

#include "AudioOutput.h"
#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace yffplayer {

class IOSAudioOutput : public AudioOutput {
public:
    IOSAudioOutput();
    ~IOSAudioOutput();

    bool init(int sampleRate, int channels) override;
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    bool enqueueAudioFrame(const AudioFrame& frame) override;

private:
    static void AudioQueueCallback(void* userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);
    void handleBuffer(AudioQueueBufferRef inBuffer);

    bool mRunning = false;
    bool mPaused = false;

    int mSampleRate = 0;
    int mChannels = 0;
    UInt32 mFrameBytes = 0;

    AudioQueueRef mAudioQueue = nullptr;
    static constexpr int kNumBuffers = 3;
    AudioQueueBufferRef mBuffers[kNumBuffers];

    std::mutex mMutex;
    std::condition_variable mCond;
    std::deque<AudioFrame> mFrameQueue;
    const size_t mMaxQueueSize = 20;
};

} // namespace yffplayer
