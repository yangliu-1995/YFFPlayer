#include "IOSAudioRenderer.h"
#include <stdexcept>
#include <cstring>

namespace yffplayer {

IOSAudioRenderer::IOSAudioRenderer() {
    std::memset(buffers_, 0, sizeof(buffers_));
}

IOSAudioRenderer::~IOSAudioRenderer() {
    release();
}

bool IOSAudioRenderer::init(int sampleRate, int channels, int bitsPerSample,
                           std::shared_ptr<RendererCallback> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioQueue_) {
        return false;
    }

    sampleRate_ = sampleRate;
    channels_ = channels;
    bitsPerSample_ = bitsPerSample;
    callback_ = callback;

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBitsPerChannel = bitsPerSample;
    format.mChannelsPerFrame = channels;
    format.mBytesPerFrame = (bitsPerSample / 8) * channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = format.mBytesPerFrame;

    OSStatus status = AudioQueueNewOutput(&format,
                                         AudioQueueOutputCallback,
                                         this,
                                         nullptr,
                                         kCFRunLoopCommonModes,
                                         0,
                                         &audioQueue_);
    if (status != noErr) {
        printf("IOSAudioRenderer: Failed to create AudioQueue, status=%d\n", status);
        return false;
    }

    // 增加缓冲区数量和大小
    static constexpr int kNumBuffers = 5; // 增加到5个缓冲区
    static constexpr float bufferDurationInSeconds = 0.3f; // 增加到300毫秒
    
    UInt32 bufferSize = sampleRate * channels * (bitsPerSample / 8) * bufferDurationInSeconds;
    printf("IOSAudioRenderer: 使用缓冲区大小: %u 字节 (%.1f毫秒)\n", 
           bufferSize, bufferDurationInSeconds * 1000);
    
    // 分配缓冲区
    for (int i = 0; i < kNumBuffers; ++i) {
        status = AudioQueueAllocateBuffer(audioQueue_, bufferSize, &buffers_[i]);
        if (status != noErr) {
            printf("IOSAudioRenderer: Failed to allocate buffer %d, status=%d\n", i, status);
            release();
            return false;
        }
        std::memset(buffers_[i]->mAudioData, 0, bufferSize); // 清零缓冲区
        buffers_[i]->mAudioDataByteSize = 0;
        AudioQueueEnqueueBuffer(audioQueue_, buffers_[i], 0, nullptr);
    }

//    setVolume(volume_);
//    setMute(isMuted_);

    status = AudioQueueStart(audioQueue_, nullptr);
    if (status != noErr) {
        printf("IOSAudioRenderer: Failed to start AudioQueue, status=%d\n", status);
        release();
        return false;
    }
    isPlaying_ = true;

    return true;
}

bool IOSAudioRenderer::play(const AudioFrame& frame) {
//    std::lock_guard<std::mutex> lock(mutex_);

    if (!audioQueue_) {
        return false;
    }

    if (!isPlaying_) {
        OSStatus status = AudioQueueStart(audioQueue_, nullptr);
        if (status != noErr) {
            return false;
        }
        isPlaying_ = true;
    }

    // 将帧加入队列
    frameQueue_.push(frame);

    // 尝试填充空闲缓冲区
    return enqueueAudioFrame(frame);
}

void IOSAudioRenderer::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioQueue_ && isPlaying_) {
        AudioQueuePause(audioQueue_);
        isPlaying_ = false;
    }
}

void IOSAudioRenderer::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioQueue_ && !isPlaying_) {
        AudioQueueStart(audioQueue_, nullptr);
        isPlaying_ = true;
    }
}

void IOSAudioRenderer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioQueue_) {
        AudioQueueStop(audioQueue_, true);
        isPlaying_ = false;
        // 清空帧队列
        while (!frameQueue_.empty()) {
            frameQueue_.pop();
        }
    }
}

void IOSAudioRenderer::setVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (audioQueue_ && !isMuted_) {
        AudioQueueSetParameter(audioQueue_, kAudioQueueParam_Volume, volume_);
    }
}

float IOSAudioRenderer::getVolume() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return volume_;
}

void IOSAudioRenderer::setMute(bool mute) {
    std::lock_guard<std::mutex> lock(mutex_);
    isMuted_ = mute;
    if (audioQueue_) {
        AudioQueueSetParameter(audioQueue_, kAudioQueueParam_Volume,
                              isMuted_ ? 0.0f : volume_);
    }
}

bool IOSAudioRenderer::isMuted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isMuted_;
}

void IOSAudioRenderer::release() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioQueue_) {
        AudioQueueStop(audioQueue_, true);
        for (int i = 0; i < kNumBuffers; ++i) {
            if (buffers_[i]) {
                AudioQueueFreeBuffer(audioQueue_, buffers_[i]);
                buffers_[i] = nullptr;
            }
        }
        AudioQueueDispose(audioQueue_, true);
        audioQueue_ = nullptr;
    }
    while (!frameQueue_.empty()) {
        frameQueue_.pop();
    }
    isPlaying_ = false;
}

void IOSAudioRenderer::AudioQueueOutputCallback(void* inUserData,
                                               AudioQueueRef inAQ,
                                               AudioQueueBufferRef inBuffer) {
    NSLog(@"AudioQueueOutputCallback");
    IOSAudioRenderer* renderer = static_cast<IOSAudioRenderer*>(inUserData);
    renderer->handleBufferCompleted(inBuffer);
}

void IOSAudioRenderer::handleBufferCompleted(AudioQueueBufferRef buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    printf("IOSAudioRenderer: Buffer completed, frameQueue size=%zu\n", frameQueue_.size());

    // 通知已播放的帧
    if (!frameQueue_.empty()) {
        AudioFrame frame = frameQueue_.back();
//        frameQueue_.pop();
        
        if (callback_) {
            printf("IOSAudioRenderer: Notifying frame rendered, PTS=%lld\n", frame.pts);
            callback_->onAudioFrameRendered(frame);
        }
        
        // 释放帧数据内存
//        delete[] frame.data;
        frame.data = nullptr;
    } else {
        printf("IOSAudioRenderer: frameQueue is empty\n");
        // 如果队列为空，填充静音数据避免爆音
        std::memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
        AudioQueueEnqueueBuffer(audioQueue_, buffer, 0, nullptr);
        return;
    }

    // 检查 AudioQueue 是否运行
    UInt32 isRunning = 0;
    UInt32 propSize = sizeof(isRunning);
    OSStatus status = AudioQueueGetProperty(audioQueue_, kAudioQueueProperty_IsRunning, &isRunning, &propSize);
    if (status != noErr || !isRunning) {
        printf("IOSAudioRenderer: AudioQueue not running, skipping enqueue\n");
        return;
    }

    // 清空缓冲区
    std::memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
    buffer->mAudioDataByteSize = 0;

    // 填充新数据
    if (!frameQueue_.empty()) {
        const AudioFrame& nextFrame = frameQueue_.front();
        frameQueue_.pop();
        if (nextFrame.sampleRate != sampleRate_ || nextFrame.channels != channels_ ||
            nextFrame.bitDepth != bitsPerSample_) {
            printf("IOSAudioRenderer: Invalid frame format - 预期(%d,%d,%d) 实际(%d,%d,%d)\n", 
                   sampleRate_, channels_, bitsPerSample_,
                   nextFrame.sampleRate, nextFrame.channels, nextFrame.bitDepth);
        } else {
            UInt32 bytesToCopy = std::min(static_cast<UInt32>(nextFrame.size),
                                         buffer->mAudioDataBytesCapacity);
            std::memcpy(buffer->mAudioData, nextFrame.data, bytesToCopy);
            buffer->mAudioDataByteSize = bytesToCopy;
            printf("IOSAudioRenderer: Filled buffer with %u bytes, PTS=%lld\n",
                   bytesToCopy, nextFrame.pts);
        }
    } else {
        // 如果队列为空，填充静音数据
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
        std::memset(buffer->mAudioData, 0, buffer->mAudioDataByteSize);
        printf("IOSAudioRenderer: No frames available, filling with silence\n");
    }

    // 重新入队缓冲区
    status = AudioQueueEnqueueBuffer(audioQueue_, buffer, 0, nullptr);
    if (status != noErr) {
        printf("IOSAudioRenderer: Failed to re-enqueue buffer, status=%d\n", status);
    } else {
        printf("IOSAudioRenderer: Buffer re-enqueued\n");
    }
}

bool IOSAudioRenderer::enqueueAudioFrame(const AudioFrame& frame) {
    // 查找空闲缓冲区
    for (int i = 0; i < kNumBuffers; ++i) {
        if (buffers_[i]->mAudioDataByteSize == 0) {
            // 验证帧格式
            if (frame.sampleRate != sampleRate_ || frame.channels != channels_ ||
                frame.bitDepth != bitsPerSample_) {
                return false;
            }

            // 填充缓冲区
            int64_t bytesToCopy = frame.size;
            std::memcpy(buffers_[i]->mAudioData, frame.data, bytesToCopy);
            buffers_[i]->mAudioDataByteSize = bytesToCopy;

            // 入队缓冲区
            OSStatus status = AudioQueueEnqueueBuffer(audioQueue_, buffers_[i], 0, nullptr);
            if (status != noErr) {
                return false;
            }
            return true;
        }
    }
    printf("IOSAudioRenderer: 没有空闲缓冲区，帧可能被丢弃\n");
    return false; // 没有空闲缓冲区
}

} // namespace yffplayer
