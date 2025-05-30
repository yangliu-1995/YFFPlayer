#pragma once

#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

#include "AudioFrame.h"
#include "AudioOutput.h"

class IOSAUAudioOutput : public yffplayer::AudioOutput {
public:
    IOSAUAudioOutput();
    ~IOSAUAudioOutput();

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
    static OSStatus AudioUnitRenderCallback(void* inRefCon,
                                            AudioUnitRenderActionFlags* ioActionFlags,
                                            const AudioTimeStamp* inTimeStamp, UInt32 inBusNumber,
                                            UInt32 inNumberFrames, AudioBufferList* ioData);

    void fillBuffer(AudioBufferList* ioData, UInt32 inNumberFrames);

    AudioUnit mAudioUnit = nullptr;

    int mSampleRate = 0;
    int mChannels = 0;
    int mBytesPerFrame = 0;

    std::atomic<bool> mIsRunning{false};
    std::atomic<bool> mIsPaused{false};

    float mVolume = 1.0f;
    bool mMute = false;

    std::mutex mMutex;
    std::condition_variable mCond;
    std::deque<std::shared_ptr<yffplayer::AudioFrame>> mFrameQueue;
    yffplayer::AudioPlaybackCallback mPlaybackCallback;
};
