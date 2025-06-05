#include "IOSAudioOutput.h"
#include <algorithm>
#include <cstring>
#import <AVFoundation/AVFoundation.h>
#import <iostream>

IOSAudioOutput::IOSAudioOutput() : mVolume(1.0f), mMute(false) {}

IOSAudioOutput::~IOSAudioOutput() { stop(); }

bool IOSAudioOutput::init(int sampleRate, int channels) {
    mSampleRate = sampleRate;
    mChannels = channels;
    mFrameBytes = (UInt32)(sampleRate * 0.5 * channels * 2);  // 0.2s of audio data
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:nil];
    [[AVAudioSession sharedInstance] setActive:YES error:nil];

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBitsPerChannel = 16;
    format.mChannelsPerFrame = channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = channels * 2;
    format.mBytesPerPacket = channels * 2;

    OSStatus status =
        AudioQueueNewOutput(&format, AudioQueueCallback, this, nullptr, nullptr, 0, &mAudioQueue);
    if (status != noErr) {
        return false;
    }

    for (int i = 0; i < kNumBuffers; ++i) {
        status = AudioQueueAllocateBuffer(mAudioQueue, mFrameBytes, &mBuffers[i]);
        if (status != noErr) {
            return false;
        }
    }

    // 初始化音量参数
    setVolume(mVolume);

    return true;
}

void IOSAudioOutput::start() {
    if (mRunning) return;

    mRunning = true;
    mPaused = false;

    for (int i = 0; i < kNumBuffers; ++i) {
        handleBuffer(mBuffers[i]);
    }

    AudioQueueStart(mAudioQueue, nullptr);
}

void IOSAudioOutput::stop() {
    if (!mRunning) return;

    mRunning = false;
    AudioQueueStop(mAudioQueue, true);
    AudioQueueDispose(mAudioQueue, true);
    mAudioQueue = nullptr;

    std::lock_guard<std::mutex> lock(mMutex);
    mFrameQueue.clear();
    mCond.notify_one();
}

void IOSAudioOutput::pause() {
    if (mPaused || !mRunning) return;
    AudioQueuePause(mAudioQueue);
    mPaused = true;
}

void IOSAudioOutput::resume() {
    if (!mPaused || !mRunning) return;
    AudioQueueStart(mAudioQueue, nullptr);
    mPaused = false;
    std::lock_guard<std::mutex> lock(mMutex);
    mCond.notify_one();
}

void IOSAudioOutput::flush() {
    std::lock_guard<std::mutex> lock(mMutex);
    mFrameQueue.clear();
    AudioQueueFlush(mAudioQueue);
    mCond.notify_one();
}

bool IOSAudioOutput::enqueueAudioFrame(const yffplayer::AudioFrame& frame) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCond.wait(lock, [&] { return mFrameQueue.size() < mMaxQueueSize || !mRunning; });
    if (!mRunning) return false;
    mFrameQueue.push_back(frame);
    mCond.notify_one();
    return true;
}

void IOSAudioOutput::setPlaybackCallback(yffplayer::AudioPlaybackCallback callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mPlaybackCallback = callback;
}

void IOSAudioOutput::AudioQueueCallback(void* userData, AudioQueueRef inAQ,
                                        AudioQueueBufferRef inBuffer) {
    auto* output = static_cast<IOSAudioOutput*>(userData);
    output->handleBuffer(inBuffer);
}
static CFAbsoluteTime baseTime = 0;
void IOSAudioOutput::handleBuffer(AudioQueueBufferRef inBuffer) {
    yffplayer::AudioFrame frame;
    bool hasFrame = false;

    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mRunning) return;

        if (mFrameQueue.empty()) {
            UInt32 silenceBytes = std::min((UInt32)512, inBuffer->mAudioDataBytesCapacity);
            memset(inBuffer->mAudioData, 0, silenceBytes);
            inBuffer->mAudioDataByteSize = silenceBytes;
        } else {
            frame = mFrameQueue.front();
            mFrameQueue.pop_front();
            mCond.notify_one();
            hasFrame = true;

            size_t dataSize = std::min(frame.mData.size(), (size_t)mFrameBytes);
            memcpy(inBuffer->mAudioData, frame.mData.data(), dataSize);
            inBuffer->mAudioDataByteSize = (UInt32)dataSize;
        }
        AudioQueueEnqueueBuffer(mAudioQueue, inBuffer, 0, nullptr);
    }

    // 在锁外调用回调，避免死锁
    if (hasFrame && mPlaybackCallback) {
        if (baseTime == 0) {
            baseTime = CFAbsoluteTimeGetCurrent();
        }
        mPlaybackCallback(frame.mPts, frame.mDuration);
    }
}

void IOSAudioOutput::setVolume(float volume) {
    if (volume < 0.f) volume = 0.f;
    if (volume > 1.f) volume = 1.f;
    mVolume = volume;

    if (mMute) volume = 0.f;

    if (mAudioQueue) {
        OSStatus status = AudioQueueSetParameter(mAudioQueue, kAudioQueueParam_Volume, volume);
        if (status != noErr) {
            // 可选择记录日志
        }
    }
}

void IOSAudioOutput::setMute(bool mute) {
    mMute = mute;

    float volume = mute ? 0.f : mVolume;

    if (mAudioQueue) {
        OSStatus status = AudioQueueSetParameter(mAudioQueue, kAudioQueueParam_Volume, volume);
        if (status != noErr) {
            // 可选择记录日志
        }
    }
}
