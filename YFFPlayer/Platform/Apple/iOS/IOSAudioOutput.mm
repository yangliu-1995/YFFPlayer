#include "IOSAudioOutput.h"

#include "Log.h"
#include <algorithm>
#include <cstring>
#import <AVFoundation/AVFoundation.h>

IOSAudioOutput::IOSAudioOutput() : volume_(1.0f), mute_(false) {}

IOSAudioOutput::~IOSAudioOutput() {
    stop();
    LogInfo << "~IOSAudioOutput";
}

bool IOSAudioOutput::init(int sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = channels;
    frameBytes_ = (UInt32)(sampleRate * 0.2 * channels * 2);  // 0.2s of audio data
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
        AudioQueueNewOutput(&format, AudioQueueCallback, this, nullptr, nullptr, 0, &audioQueue_);
    if (status != noErr) {
        return false;
    }

    for (int i = 0; i < kNumBuffers; ++i) {
        status = AudioQueueAllocateBuffer(audioQueue_, frameBytes_, &buffers_[i]);
        if (status != noErr) {
            return false;
        }
    }

    // 初始化音量参数
    setVolume(volume_);

    return true;
}

void IOSAudioOutput::start() {
    if (running_) return;

    running_ = true;
    paused_ = false;

    for (int i = 0; i < kNumBuffers; ++i) {
        handleBuffer(buffers_[i]);
    }

    AudioQueueStart(audioQueue_, nullptr);
}

void IOSAudioOutput::stop() {
    if (!running_) return;

    running_ = false;
    AudioQueueStop(audioQueue_, true);
    AudioQueueDispose(audioQueue_, true);
    audioQueue_ = nullptr;

    std::lock_guard<std::mutex> lock(mutex_);
    frameQueue_.clear();
    cond_.notify_one();
}

void IOSAudioOutput::pause() {
    if (paused_ || !running_) return;
    AudioQueuePause(audioQueue_);
    paused_ = true;
}

void IOSAudioOutput::resume() {
    if (!paused_ || !running_) return;
    AudioQueueStart(audioQueue_, nullptr);
    paused_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    cond_.notify_one();
}

void IOSAudioOutput::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    frameQueue_.clear();
    AudioQueueFlush(audioQueue_);
    cond_.notify_one();
}

bool IOSAudioOutput::enqueueAudioFrame(const yffplayer::AudioFrame& frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [&] { return frameQueue_.size() < maxQueueSize_ || !running_; });
    if (!running_) return false;
    frameQueue_.push_back(frame);
    cond_.notify_one();
    return true;
}

void IOSAudioOutput::setPlaybackCallback(yffplayer::AudioPlaybackCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    playbackCallback_ = callback;
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
        std::unique_lock<std::mutex> lock(mutex_);
        if (!running_) return;

        if (frameQueue_.empty()) {
            UInt32 silenceBytes = std::min((UInt32)512, inBuffer->mAudioDataBytesCapacity);
            memset(inBuffer->mAudioData, 0, silenceBytes);
            inBuffer->mAudioDataByteSize = silenceBytes;
        } else {
            frame = frameQueue_.front();
            frameQueue_.pop_front();
            cond_.notify_one();
            hasFrame = true;

            size_t dataSize = std::min(frame.data_.size(), (size_t)frameBytes_);
            memcpy(inBuffer->mAudioData, frame.data_.data(), dataSize);
            inBuffer->mAudioDataByteSize = (UInt32)dataSize;
        }
        AudioQueueEnqueueBuffer(audioQueue_, inBuffer, 0, nullptr);
    }

    // 在锁外调用回调，避免死锁
    if (hasFrame && playbackCallback_) {
        if (baseTime == 0) {
            baseTime = CFAbsoluteTimeGetCurrent();
        }
        playbackCallback_(frame.pts_, frame.duration_);
    }
}

void IOSAudioOutput::setVolume(float volume) {
    if (volume < 0.f) volume = 0.f;
    if (volume > 1.f) volume = 1.f;
    volume_ = volume;

    if (mute_) volume = 0.f;

    if (audioQueue_) {
        OSStatus status = AudioQueueSetParameter(audioQueue_, kAudioQueueParam_Volume, volume);
        if (status != noErr) {
            // 可选择记录日志
        }
    }
}

void IOSAudioOutput::setMute(bool mute) {
    mute_ = mute;

    float volume = mute ? 0.f : volume_;

    if (audioQueue_) {
        OSStatus status = AudioQueueSetParameter(audioQueue_, kAudioQueueParam_Volume, volume);
        if (status != noErr) {
            // 可选择记录日志
        }
    }
}
