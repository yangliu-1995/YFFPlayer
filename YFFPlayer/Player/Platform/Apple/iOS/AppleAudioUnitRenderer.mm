#include "AppleAudioUnitRenderer.h"
#include <cstring>
#include <algorithm>

namespace yffplayer {

AppleAudioUnitRenderer::AppleAudioUnitRenderer() : audioUnit_(nullptr) {
    // 预分配渲染缓冲区
    renderBuffer_.resize(kMaxBufferSize);
}

AppleAudioUnitRenderer::~AppleAudioUnitRenderer() {
    release();
}

bool AppleAudioUnitRenderer::init(int sampleRate, int channels, int bitsPerSample,
                                 std::shared_ptr<RendererCallback> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 保存音频参数
    sampleRate_ = sampleRate;
    channels_ = channels;
    bitsPerSample_ = bitsPerSample;
    callback_ = callback;
    
    NSLog(@"AppleAudioUnitRenderer: 初始化音频 - 采样率=%d, 通道数=%d, 位深=%d", 
          sampleRate, channels, bitsPerSample);
    
    // 设置 AudioUnit
    if (!setupAudioUnit()) {
        NSLog(@"AppleAudioUnitRenderer: 设置 AudioUnit 失败");
        return false;
    }
    
    // 设置音量
//    setVolume(volume_);
    
    return true;
}

bool AppleAudioUnitRenderer::setupAudioUnit() {
    // 创建音频组件描述
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    
#if TARGET_OS_IPHONE
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
    
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    
    // 查找音频组件
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component) {
        NSLog(@"AppleAudioUnitRenderer: 找不到合适的音频组件");
        return false;
    }
    
    // 创建音频单元实例
    OSStatus status = AudioComponentInstanceNew(component, &audioUnit_);
    if (status != noErr) {
        NSLog(@"AppleAudioUnitRenderer: 创建音频单元实例失败, status=%d", (int)status);
        return false;
    }
    
    // 设置 IO 格式
    AudioStreamBasicDescription audioFormat;
    setupAudioFormat(audioFormat, sampleRate_, channels_, bitsPerSample_);
    
    status = AudioUnitSetProperty(audioUnit_,
                                 kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Input,
                                 0,
                                 &audioFormat,
                                 sizeof(audioFormat));
    if (status != noErr) {
        NSLog(@"AppleAudioUnitRenderer: 设置音频格式失败, status=%d", (int)status);
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        return false;
    }
    
    // 设置渲染回调
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = RenderCallback;
    callbackStruct.inputProcRefCon = this;
    
    status = AudioUnitSetProperty(audioUnit_,
                                 kAudioUnitProperty_SetRenderCallback,
                                 kAudioUnitScope_Input,
                                 0,
                                 &callbackStruct,
                                 sizeof(callbackStruct));
    if (status != noErr) {
        NSLog(@"AppleAudioUnitRenderer: 设置渲染回调失败, status=%d", (int)status);
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        return false;
    }
    
    // 初始化 AudioUnit
    status = AudioUnitInitialize(audioUnit_);
    if (status != noErr) {
        NSLog(@"AppleAudioUnitRenderer: 初始化 AudioUnit 失败, status=%d", (int)status);
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        return false;
    }
    
    // 启动 AudioUnit
    status = AudioOutputUnitStart(audioUnit_);
    if (status != noErr) {
        NSLog(@"AppleAudioUnitRenderer: 启动 AudioUnit 失败, status=%d", (int)status);
        AudioUnitUninitialize(audioUnit_);
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        return false;
    }
    
    isPlaying_ = true;
    NSLog(@"AppleAudioUnitRenderer: AudioUnit 设置成功并开始播放");
    return true;
}

void AppleAudioUnitRenderer::setupAudioFormat(AudioStreamBasicDescription& format, 
                                             int sampleRate, int channels, int bitsPerSample) {
    std::memset(&format, 0, sizeof(format));
    
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mBitsPerChannel = bitsPerSample;
    format.mChannelsPerFrame = channels;
    format.mBytesPerFrame = (bitsPerSample / 8) * channels;
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = format.mBytesPerFrame;
}

bool AppleAudioUnitRenderer::play(const AudioFrame& frame) {
    if (!audioUnit_) {
        NSLog(@"AppleAudioUnitRenderer: AudioUnit 未初始化");
        return false;
    }
    
    if (!isPlaying_) {
        resume();
    }
    
    // 创建一个拷贝，以便管理内存
    AudioFrame frameCopy = frame;
    // 为音频数据分配新内存并复制
    uint8_t* dataCopy = new uint8_t[frame.size];
    std::memcpy(dataCopy, frame.data, frame.size);
    frameCopy.data = dataCopy;
    
    // 将帧加入队列
//    std::lock_guard<std::mutex> lock(mutex_);
    frameQueue_.push(frameCopy);
    
    NSLog(@"AppleAudioUnitRenderer: 添加音频帧到队列, PTS=%lld, 大小=%lld, 队列大小=%lu", 
          frame.pts, frame.size, (unsigned long)frameQueue_.size());
    
    return true;
}

void AppleAudioUnitRenderer::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioUnit_ && isPlaying_) {
        OSStatus status = AudioOutputUnitStop(audioUnit_);
        if (status != noErr) {
            NSLog(@"AppleAudioUnitRenderer: 暂停 AudioUnit 失败, status=%d", (int)status);
        } else {
            isPlaying_ = false;
            NSLog(@"AppleAudioUnitRenderer: AudioUnit 已暂停");
        }
    }
}

void AppleAudioUnitRenderer::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioUnit_ && !isPlaying_) {
        OSStatus status = AudioOutputUnitStart(audioUnit_);
        if (status != noErr) {
            NSLog(@"AppleAudioUnitRenderer: 恢复 AudioUnit 失败, status=%d", (int)status);
        } else {
            isPlaying_ = true;
            NSLog(@"AppleAudioUnitRenderer: AudioUnit 已恢复");
        }
    }
}

void AppleAudioUnitRenderer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (audioUnit_) {
        OSStatus status = AudioOutputUnitStop(audioUnit_);
        if (status != noErr) {
            NSLog(@"AppleAudioUnitRenderer: 停止 AudioUnit 失败, status=%d", (int)status);
        }
        isPlaying_ = false;
        
        // 清空帧队列并释放内存
        while (!frameQueue_.empty()) {
            AudioFrame& frame = frameQueue_.front();
            delete[] frame.data;
            frame.data = nullptr;
            frameQueue_.pop();
        }
        
        NSLog(@"AppleAudioUnitRenderer: AudioUnit 已停止，队列已清空");
    }
}

void AppleAudioUnitRenderer::setVolume(float volume) {
//    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    
    if (audioUnit_ && !isMuted_) {
        // AudioUnit 没有直接的音量控制，可以通过 kAudioUnitProperty_Volume 属性设置
        // 但这个属性不是所有 AudioUnit 都支持，所以我们在渲染回调中手动调整音量
        NSLog(@"AppleAudioUnitRenderer: 设置音量 = %.2f", volume_);
    }
}

float AppleAudioUnitRenderer::getVolume() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return volume_;
}

void AppleAudioUnitRenderer::setMute(bool mute) {
    std::lock_guard<std::mutex> lock(mutex_);
    isMuted_ = mute;
    NSLog(@"AppleAudioUnitRenderer: 设置静音 = %d", isMuted_ ? 1 : 0);
}

bool AppleAudioUnitRenderer::isMuted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isMuted_;
}

void AppleAudioUnitRenderer::release() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (audioUnit_) {
        // 停止 AudioUnit
        if (isPlaying_) {
            AudioOutputUnitStop(audioUnit_);
            isPlaying_ = false;
        }
        
        // 清理 AudioUnit
        AudioUnitUninitialize(audioUnit_);
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        
        NSLog(@"AppleAudioUnitRenderer: AudioUnit 已释放");
    }
    
    // 清空帧队列并释放内存
    while (!frameQueue_.empty()) {
        AudioFrame& frame = frameQueue_.front();
        delete[] frame.data;
        frame.data = nullptr;
        frameQueue_.pop();
    }
    
    // 清空渲染缓冲区
    renderBuffer_.clear();
    renderBuffer_.resize(kMaxBufferSize);
}

OSStatus AppleAudioUnitRenderer::RenderCallback(void* inRefCon,
                                              AudioUnitRenderActionFlags* ioActionFlags,
                                              const AudioTimeStamp* inTimeStamp,
                                              UInt32 inBusNumber,
                                              UInt32 inNumberFrames,
                                              AudioBufferList* ioData) {
    AppleAudioUnitRenderer* renderer = static_cast<AppleAudioUnitRenderer*>(inRefCon);
    return renderer->handleRenderCallback(ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, ioData);
}

OSStatus AppleAudioUnitRenderer::handleRenderCallback(AudioUnitRenderActionFlags* ioActionFlags,
                                                    const AudioTimeStamp* inTimeStamp,
                                                    UInt32 inBusNumber,
                                                    UInt32 inNumberFrames,
                                                    AudioBufferList* ioData) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 计算需要的字节数
    UInt32 bytesPerFrame = channels_ * (bitsPerSample_ / 8);
    UInt32 bytesRequired = inNumberFrames * bytesPerFrame;
    
    // 检查是否有足够的数据
    if (frameQueue_.empty() || isMuted_) {
        // 如果队列为空或静音，输出静音
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            std::memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        }
        return noErr;
    }
    
    // 从队列中获取帧并填充缓冲区
    AudioFrame& frame = frameQueue_.front();
    
    // 检查帧格式是否匹配
    if (frame.sampleRate != sampleRate_ || frame.channels != channels_ || 
        frame.bitDepth != bitsPerSample_) {
        NSLog(@"AppleAudioUnitRenderer: 帧格式不匹配 - 预期(%d,%d,%d) 实际(%d,%d,%d)",
              sampleRate_, channels_, bitsPerSample_,
              frame.sampleRate, frame.channels, frame.bitDepth);
        
        // 输出静音
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            std::memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        }
        
        // 移除不匹配的帧
        delete[] frame.data;
        frame.data = nullptr;
        frameQueue_.pop();
        
        return noErr;
    }
    
    // 计算可以复制的字节数
    UInt32 bytesToCopy = std::min(static_cast<UInt32>(frame.size), bytesRequired);
    
    // 应用音量
    if (volume_ < 1.0f && !isMuted_) {
        // 复制数据到临时缓冲区
        std::memcpy(renderBuffer_.data(), frame.data, bytesToCopy);
        
        // 应用音量（假设是16位有符号整数格式）
        if (bitsPerSample_ == 16) {
            int16_t* samples = reinterpret_cast<int16_t*>(renderBuffer_.data());
            int numSamples = bytesToCopy / sizeof(int16_t);
            
            for (int i = 0; i < numSamples; i++) {
                samples[i] = static_cast<int16_t>(samples[i] * volume_);
            }
        }
        
        // 复制处理后的数据到输出缓冲区
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            std::memcpy(ioData->mBuffers[i].mData, renderBuffer_.data(), 
                       std::min(bytesToCopy, ioData->mBuffers[i].mDataByteSize));
        }
    } else {
        // 直接复制数据到输出缓冲区
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            std::memcpy(ioData->mBuffers[i].mData, frame.data, 
                       std::min(bytesToCopy, ioData->mBuffers[i].mDataByteSize));
        }
    }
    
    // 通知帧已渲染
    if (callback_) {
        callback_->onAudioFrameRendered(frame);
    }
    
    // 释放帧内存并从队列中移除
    delete[] frame.data;
    frame.data = nullptr;
    frameQueue_.pop();
    
    return noErr;
}

} // namespace yffplayer
