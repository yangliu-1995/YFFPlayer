#include "IOSAUAudioOutput.h"
#include <algorithm>
#include <cstring>
#include <iostream>

IOSAUAudioOutput::IOSAUAudioOutput()
    : mAudioUnit(nullptr), mSampleRate(0), mChannels(0), mBytesPerFrame(0),
      mIsRunning(false), mIsPaused(false), mVolume(1.0f), mMute(false) {}

IOSAUAudioOutput::~IOSAUAudioOutput() {
    stop();
}

bool IOSAUAudioOutput::init(int sampleRate, int channels) {
    mSampleRate = sampleRate;
    mChannels = channels;
    mBytesPerFrame = mChannels * sizeof(int16_t);

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) {
        std::cerr << "Failed to find audio component" << std::endl;
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(comp, &mAudioUnit);
    if (status != noErr) {
        std::cerr << "AudioComponentInstanceNew failed: " << status << std::endl;
        return false;
    }

    AudioStreamBasicDescription audioFormat{};
    audioFormat.mSampleRate = mSampleRate;
    audioFormat.mFormatID = kAudioFormatLinearPCM;
    audioFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    audioFormat.mBitsPerChannel = 16;
    audioFormat.mChannelsPerFrame = mChannels;
    audioFormat.mFramesPerPacket = 1;
    audioFormat.mBytesPerFrame = mBytesPerFrame;
    audioFormat.mBytesPerPacket = mBytesPerFrame;
    audioFormat.mReserved = 0;

    status = AudioUnitSetProperty(mAudioUnit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &audioFormat,
                                  sizeof(audioFormat));
    if (status != noErr) {
        std::cerr << "AudioUnitSetProperty(StreamFormat) failed: " << status << std::endl;
        AudioComponentInstanceDispose(mAudioUnit);
        mAudioUnit = nullptr;
        return false;
    }

    AURenderCallbackStruct callbackStruct{};
    callbackStruct.inputProc = AudioUnitRenderCallback;
    callbackStruct.inputProcRefCon = this;

    status = AudioUnitSetProperty(mAudioUnit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input,
                                  0,
                                  &callbackStruct,
                                  sizeof(callbackStruct));
    if (status != noErr) {
        std::cerr << "AudioUnitSetProperty(SetRenderCallback) failed: " << status << std::endl;
        AudioComponentInstanceDispose(mAudioUnit);
        mAudioUnit = nullptr;
        return false;
    }

    status = AudioUnitInitialize(mAudioUnit);
    if (status != noErr) {
        std::cerr << "AudioUnitInitialize failed: " << status << std::endl;
        AudioComponentInstanceDispose(mAudioUnit);
        mAudioUnit = nullptr;
        return false;
    }

    return true;
}

void IOSAUAudioOutput::start() {
    if (mIsRunning.load()) return;

    mIsPaused.store(false);
    OSStatus status = AudioOutputUnitStart(mAudioUnit);
    if (status == noErr) {
        mIsRunning.store(true);
    } else {
        std::cerr << "AudioOutputUnitStart failed: " << status << std::endl;
    }
}

void IOSAUAudioOutput::stop() {
    if (!mIsRunning.load()) return;

    AudioOutputUnitStop(mAudioUnit);
    AudioUnitUninitialize(mAudioUnit);
    AudioComponentInstanceDispose(mAudioUnit);
    mAudioUnit = nullptr;

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mFrameQueue.clear();
    }
    mIsRunning.store(false);
}

void IOSAUAudioOutput::pause() {
    if (!mIsRunning.load()) return;
    mIsPaused.store(true);
}

void IOSAUAudioOutput::resume() {
    if (!mIsRunning.load()) return;
    mIsPaused.store(false);
}

void IOSAUAudioOutput::setVolume(float volume) {
    if (volume < 0.f) volume = 0.f;
    if (volume > 1.f) volume = 1.f;
    mVolume = volume;

    if (mAudioUnit) {
        OSStatus status = AudioUnitSetParameter(mAudioUnit,
                                                kHALOutputParam_Volume,
                                                kAudioUnitScope_Global,
                                                0,
                                                mMute ? 0.0f : mVolume,
                                                0);
        if (status != noErr) {
            // 失败时不处理，也可以考虑日志
        }
    }
}

void IOSAUAudioOutput::setMute(bool mute) {
    mMute = mute;

    if (mAudioUnit) {
        float vol = mMute ? 0.0f : mVolume;
        OSStatus status = AudioUnitSetParameter(mAudioUnit,
                                                kHALOutputParam_Volume,
                                                kAudioUnitScope_Global,
                                                0,
                                                vol,
                                                0);
        if (status != noErr) {
            // 同上
        }
    }
}

bool IOSAUAudioOutput::enqueueAudioFrame(const yffplayer::AudioFrame& frame) {
    if (!mIsRunning.load()) return false;

    std::unique_lock<std::mutex> lock(mMutex);
    mCond.wait(lock, [this]() {
        return mFrameQueue.size() < 50 || !mIsRunning.load();
    });

    if (!mIsRunning.load()) return false;

    mFrameQueue.push_back(std::make_shared<yffplayer::AudioFrame>(frame));
    mCond.notify_one();
    return true;
}

OSStatus IOSAUAudioOutput::AudioUnitRenderCallback(void* inRefCon,
                                                   AudioUnitRenderActionFlags* ioActionFlags,
                                                   const AudioTimeStamp* inTimeStamp,
                                                   UInt32 inBusNumber,
                                                   UInt32 inNumberFrames,
                                                   AudioBufferList* ioData) {
    auto* output = static_cast<IOSAUAudioOutput*>(inRefCon);
    output->fillBuffer(ioData, inNumberFrames);
    return noErr;
}

void IOSAUAudioOutput::fillBuffer(AudioBufferList* ioData, UInt32 inNumberFrames) {
    std::unique_lock<std::mutex> lock(mMutex);

    if (mIsPaused.load()) {
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        }
        return;
    }

    size_t requestedBytes = inNumberFrames * mBytesPerFrame;
    size_t copiedBytes = 0;
    uint8_t* outPtr = static_cast<uint8_t*>(ioData->mBuffers[0].mData);

    while (copiedBytes < requestedBytes) {
        if (mFrameQueue.empty()) {
            memset(outPtr + copiedBytes, 0, requestedBytes - copiedBytes);
            break;
        }

        auto frame = mFrameQueue.front();

        size_t frameSize = frame->mData.size();
        size_t copySize = std::min(frameSize, requestedBytes - copiedBytes);

        memcpy(outPtr + copiedBytes, frame->mData.data(), copySize);

        if (copySize < frameSize) {
            frame->mData.erase(frame->mData.begin(), frame->mData.begin() + copySize);
        } else {
            mFrameQueue.pop_front();
            mCond.notify_all();
        }
        copiedBytes += copySize;
    }
}
