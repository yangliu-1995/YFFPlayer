#include "IOSAudioOutput.h"
#include <algorithm>
#include <cstring>

using namespace yffplayer;

IOSAudioOutput::IOSAudioOutput() {}

IOSAudioOutput::~IOSAudioOutput() {
    stop();
}

bool IOSAudioOutput::init(int sampleRate, int channels) {
    mSampleRate = sampleRate;
    mChannels = channels;
    mFrameBytes = (UInt32)(sampleRate * 0.5 * channels * 2); // 0.5s of audio data

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBitsPerChannel = 16;
    format.mChannelsPerFrame = channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = channels * 2;
    format.mBytesPerPacket = channels * 2;

    OSStatus status = AudioQueueNewOutput(&format, AudioQueueCallback, this, nullptr, nullptr, 0, &mAudioQueue);
    if (status != noErr) {
        return false;
    }

    for (int i = 0; i < kNumBuffers; ++i) {
        status = AudioQueueAllocateBuffer(mAudioQueue, mFrameBytes, &mBuffers[i]);
        if (status != noErr) {
            return false;
        }
    }

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
}

bool IOSAudioOutput::enqueueAudioFrame(const AudioFrame& frame) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCond.wait(lock, [&] { return mFrameQueue.size() < mMaxQueueSize || !mRunning; });
    if (!mRunning) return false;
    mFrameQueue.push_back(frame);
    mCond.notify_all();
    return true;
}

void IOSAudioOutput::AudioQueueCallback(void* userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    auto* output = static_cast<IOSAudioOutput*>(userData);
    output->handleBuffer(inBuffer);
}

void IOSAudioOutput::handleBuffer(AudioQueueBufferRef inBuffer) {
    AudioFrame frame;

    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mRunning) return;

        if (mFrameQueue.empty()) {
            memset(inBuffer->mAudioData, 0, inBuffer->mAudioDataBytesCapacity);
            inBuffer->mAudioDataByteSize = inBuffer->mAudioDataBytesCapacity;
        } else {
            frame = mFrameQueue.front();
            mFrameQueue.pop_front();
            mCond.notify_all();

            size_t dataSize = std::min(frame.mData.size(), (size_t)mFrameBytes);
            memcpy(inBuffer->mAudioData, frame.mData.data(), dataSize);
            inBuffer->mAudioDataByteSize = (UInt32)dataSize;
        }
    }

    AudioQueueEnqueueBuffer(mAudioQueue, inBuffer, 0, nullptr);
}
