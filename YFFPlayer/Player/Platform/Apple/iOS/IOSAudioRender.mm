#import "IOSAudioRender.h"
#import <AVFoundation/AVFoundation.h>
#import "AudioFrame.h"
#include <deque>

#define kNumberBuffers 3

@interface IOSAudioRender () {
    AudioQueueRef _audioQueue;
    AudioQueueBufferRef _buffers[kNumberBuffers];
    UInt32 _frameBytes;
    std::deque<std::shared_ptr<yffplayer::AudioFrame>> *_frameBuffer; // C++ deque
}

@property (nonatomic, assign) int sampleRate;
@property (nonatomic, assign) int channels;
@property (nonatomic, copy) IOSAudioRenderFrameProvider frameProvider;
@property (nonatomic, strong) dispatch_queue_t frameQueue;
@property (nonatomic, assign) BOOL isRunning;

@end

@implementation IOSAudioRender

static void AudioQueueCallback(void *userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    IOSAudioRender *renderer = (__bridge IOSAudioRender *)userData;
    [renderer handleBufferCallback:inBuffer];
}

- (instancetype)initWithSampleRate:(int)sampleRate
                          channels:(int)channels
                        frameBytes:(UInt32)frameBytes
                     frameProvider:(IOSAudioRenderFrameProvider)provider {
    self = [super init];
    if (self) {
        NSError *error = nil;
        AVAudioSession *session = [AVAudioSession sharedInstance];
                [session setCategory:AVAudioSessionCategoryPlayback
                         withOptions:0
                               error:&error];
                if (error) {
                    NSLog(@"Failed to set AVAudioSession category: %@", error);
                    return nil;
                }
                [session setActive:YES error:&error];
        _sampleRate = sampleRate;
        _channels = channels;
        _frameBytes = (UInt32)(sampleRate * 0.5 * channels * 2); // 0.1 seconds of audio data, 2 bytes per channel (S16)
        _frameProvider = [provider copy];
        _isRunning = NO;

        // 初始化线程安全的帧缓冲区
        _frameQueue = dispatch_queue_create("com.audio.frameQueue", DISPATCH_QUEUE_SERIAL);
        _frameBuffer = new std::deque<std::shared_ptr<yffplayer::AudioFrame>>();

        // 配置 AudioStreamBasicDescription
        AudioStreamBasicDescription format = {0};
        format.mSampleRate = sampleRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        format.mBitsPerChannel = 16;
        format.mChannelsPerFrame = channels;
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = channels * 2; // 2 bytes per channel (S16)
        format.mBytesPerPacket = channels * 2;

        // 验证格式
        NSAssert(sampleRate == 44100 && channels == 2, @"Audio format mismatch: expected 44100Hz/2ch, got %dHz/%dch", sampleRate, channels);
        NSLog(@"AudioQueue initializing with sampleRate: %d, channels: %d, frameBytes: %u", sampleRate, channels, frameBytes);

        // 创建 AudioQueue
        OSStatus status = AudioQueueNewOutput(&format, AudioQueueCallback, (__bridge void *)self, nullptr, nullptr, 0, &_audioQueue);
        if (status != noErr) {
            NSLog(@"AudioQueueNewOutput failed: %d", (int)status);
            delete _frameBuffer;
            return nil;
        }

        // 分配缓冲区
        for (int i = 0; i < kNumberBuffers; i++) {
            status = AudioQueueAllocateBuffer(_audioQueue, _frameBytes, &_buffers[i]);
            if (status != noErr) {
                NSLog(@"AudioQueueAllocateBuffer failed for buffer %d: %d", i, (int)status);
                AudioQueueDispose(_audioQueue, true);
                delete _frameBuffer;
                return nil;
            }
        }
    }
    return self;
}

- (void)dealloc {
    [self stop];
    delete _frameBuffer;
    _frameBuffer = nullptr;
    _frameQueue = nil;
}

- (void)start {
    if (_isRunning) return;

    // 检查初始帧可用性
    if (self.frameProvider) {
        self.frameProvider(self);
        dispatch_sync(_frameQueue, ^{
            if (_frameBuffer->empty()) {
                NSLog(@"Warning: No initial audio frame available");
            }
        });
    }

    _isRunning = YES;

    // 预填充缓冲区
    for (int i = 0; i < kNumberBuffers; i++) {
        [self handleBufferCallback:_buffers[i]];
    }

    // 启动 AudioQueue
    OSStatus status = AudioQueueStart(_audioQueue, NULL);
    if (status != noErr) {
        NSLog(@"AudioQueueStart failed: %d", (int)status);
        _isRunning = NO;
    } else {
        NSLog(@"AudioQueue started");
    }
}

- (void)stop {
    if (!_isRunning) return;

    _isRunning = NO;
    AudioQueueStop(_audioQueue, true);
    AudioQueueDispose(_audioQueue, true);
    _audioQueue = NULL;

    dispatch_sync(_frameQueue, ^{
        _frameBuffer->clear();
    });
    NSLog(@"AudioQueue stopped");
}

- (void)feedAudioFrame:(const std::shared_ptr<yffplayer::AudioFrame> &)frame {
    if (!frame) {
        NSLog(@"Received null audio frame");
        return;
    }
    // 记录帧信息
    NSLog(@"Received frame: pts=%lld, dur=%lld, size=%zu", frame->mPts, frame->mDuration, frame->mData.size());

    // 线程安全地将帧添加到缓冲区
    dispatch_sync(_frameQueue, ^{
        _frameBuffer->push_back(frame);
    });
}

- (void)handleBufferCallback:(AudioQueueBufferRef)inBuffer {
    if (!_isRunning) return;

    // 从帧缓冲区获取帧
    __block std::shared_ptr<yffplayer::AudioFrame> frame;
    dispatch_sync(_frameQueue, ^{
        if (!_frameBuffer->empty()) {
            frame = std::move(_frameBuffer->front()); // 使用 std::move 转移所有权
            _frameBuffer->pop_front();
        }
    });

    // 如果没有帧，尝试请求新帧
    if (!frame) {
        if (self.frameProvider) {
            self.frameProvider(self);
            dispatch_sync(_frameQueue, ^{
                if (!_frameBuffer->empty()) {
                    frame = std::move(_frameBuffer->front()); // 使用 std::move 转移所有权
                    _frameBuffer->pop_front();
                }
            });
        }
        if (!frame) {
            NSLog(@"No audio frame available, filling silence");
            memset(inBuffer->mAudioData, 0, inBuffer->mAudioDataBytesCapacity);
            inBuffer->mAudioDataByteSize = inBuffer->mAudioDataBytesCapacity;
            AudioQueueEnqueueBuffer(_audioQueue, inBuffer, 0, NULL);
            return;
        }
    }

    // 检查帧连续性
//    static int64_t lastPts = -1;
//    if (lastPts != -1 && frame->mPts != lastPts + frame->mDuration) {
//        NSLog(@"Frame discontinuity: lastPts=%lld, currentPts=%lld, dur=%lld",
//              lastPts, frame->mPts, frame->mDuration);
//    }
//    lastPts = frame->mPts;

    // 清空缓冲区
    memset(inBuffer->mAudioData, 0, inBuffer->mAudioDataBytesCapacity);

    // 拷贝数据
    NSUInteger dataSize = std::min((NSUInteger)_frameBytes, frame->mData.size());
    if (dataSize < frame->mData.size()) {
        NSLog(@"Warning: Truncating frame from %zu to %u bytes", frame->mData.size(), _frameBytes);
    }
    memcpy(inBuffer->mAudioData, frame->mData.data(), dataSize);
    inBuffer->mAudioDataByteSize = (UInt32)dataSize;

    // 入队缓冲区
    OSStatus status = AudioQueueEnqueueBuffer(_audioQueue, inBuffer, 0, NULL);
    if (status != noErr) {
        NSLog(@"AudioQueueEnqueueBuffer failed: %d", (int)status);
    }

    // 请求下一帧
    if (self.frameProvider) {
        self.frameProvider(self);
    }
}

@end
