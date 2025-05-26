//
//  TestPlayer.mm
//  TestPlayer
//
//  Created by Xueyuan Xiao on 2025/5/26.
//

#import "TestPlayer.h"
#import "Demuxer.h"
#import "MediaInfo.h"
#import "PacketQueue.h"
#import "FrameQueue.h"
#import "AudioDecoder.h"
#import "VideoDecoder.h"
#import "AudioFrame.h"
#import "VideoFrame.h"

#import "IOSAudioRender.h"
#import "IOSVideoRenderer.h"
#import "IOSCVPixelBufferVideoRenderer.h"

#import <memory>

extern "C" {
#import <libavcodec/avcodec.h>
}

@interface TestPlayer () {
    std::shared_ptr<yffplayer::Demuxer> _demuxer;
    std::shared_ptr<yffplayer::PacketQueue> _audioQueue;
    std::shared_ptr<yffplayer::PacketQueue> _videoQueue;
    std::shared_ptr<yffplayer::FrameQueue<yffplayer::AudioFrame>> _audioFrameQueue;
    std::shared_ptr<yffplayer::FrameQueue<yffplayer::VideoFrame>> _videoFrameQueue;
    std::shared_ptr<yffplayer::AudioDecoder> _audioDecoder;
    std::shared_ptr<yffplayer::VideoDecoder> _videoDecoder;
    NSThread *_audioRenderThread;
    NSThread *_videoRenderThread;
    IOSAudioRender *_audioRender;
    std::atomic<int64_t> _audioClock;
    std::atomic<int64_t> _videoClock;
    IOSVideoRenderer *_videoRenderer;
    IOSCVPixelBufferVideoRenderer *_cvVideoRender;
    yffplayer::MediaInfo _mediaInfo;
}

@end

@implementation TestPlayer

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView {
    self = [super init];
    if (self) {
        _videoRenderView = videoRenderView;
        _audioQueue = std::make_shared<yffplayer::PacketQueue>(100);
        _videoQueue = std::make_shared<yffplayer::PacketQueue>(50);

        _audioFrameQueue = std::make_shared<yffplayer::FrameQueue<yffplayer::AudioFrame>>(100);
        _videoFrameQueue = std::make_shared<yffplayer::FrameQueue<yffplayer::VideoFrame>>(50);

        _demuxer = std::make_shared<yffplayer::Demuxer>(_audioQueue, _videoQueue);
        _audioRender = [[IOSAudioRender alloc] initWithSampleRate:44100
                                                                         channels:2
                                                                       frameBytes:4096 * 2 * 2
                                                                    frameProvider:^(IOSAudioRender *renderer) {
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                NSLog(@"Audio Clock: %lld", self->_audioClock.load());
                auto audioFrame = self->_audioFrameQueue->pop();
                if (audioFrame) {
                    self->_audioClock = audioFrame->mPts + audioFrame->mDuration;
                    [renderer feedAudioFrame:audioFrame];
                }
            });
        }];
        _videoRenderer = [[IOSVideoRenderer alloc] initWithView:videoRenderView];
        _cvVideoRender = [[IOSCVPixelBufferVideoRenderer alloc] initWithView:videoRenderView];
    }
    return self;
}

- (void)playVideoWithURL:(NSURL *)url {
    yffplayer::MediaInfo mediaInfo;
    if (_demuxer->open(url.absoluteString.UTF8String, mediaInfo)) {
        _mediaInfo = mediaInfo;
        NSLog(@"Media Info, hasAudio: %d, sampleRate: %d, channels: %d, hasVideo: %d, width: %d, height: %d",
              mediaInfo.mHasAudio, mediaInfo.mAudioSampleRate, mediaInfo.mAudiochannels,
              mediaInfo.mHasVideo, mediaInfo.mVideoWidth, mediaInfo.mVideoHeight);
        _demuxer->start();
        if (mediaInfo.mHasAudio) {
            AVCodecParameters *audioCodecParams = avcodec_parameters_alloc();
            avcodec_parameters_copy(audioCodecParams, mediaInfo.mAudioCodecParameters);
            _audioDecoder = std::make_shared<yffplayer::AudioDecoder>(_audioQueue, _audioFrameQueue);
            _audioDecoder->open(audioCodecParams, mediaInfo.mAudioTimeBase);
            _audioDecoder->start();
            _audioRenderThread = [[NSThread alloc] initWithTarget:self selector:@selector(simAudioRenderThread) object:nil];
            [_audioRenderThread start];
        }
        if (mediaInfo.mHasVideo) {
            AVCodecParameters *videoCodecParams = avcodec_parameters_alloc();
            avcodec_parameters_copy(videoCodecParams, mediaInfo.mVideoCodecParameters);
            _videoDecoder = std::make_shared<yffplayer::VideoDecoder>(_videoQueue, _videoFrameQueue);
            _videoDecoder->open(videoCodecParams, mediaInfo.mVideoTimeBase);
            _videoDecoder->start();
            _videoRenderThread = [[NSThread alloc] initWithTarget:self selector:@selector(simVideoRenderThread) object:nil];
            [_videoRenderThread start];
        }
    }
}

- (void)simAudioRenderThread {
//    while (true) {
//        auto audioFrame = _audioFrameQueue->pop();
//        if (audioFrame) {
//            NSLog(@"Audio Frame: pts: %lld, duration: %lld", audioFrame->mPts, audioFrame->mDuration);
//            // Simulate audio rendering
//            [NSThread sleepForTimeInterval:audioFrame->mDuration / 1000.0];
//        }
//    }
    [_audioRender start];
}

- (void)simVideoRenderThread {
    bool hasAudio = _mediaInfo.mHasAudio;
    
    while (true) {
        auto videoFrame = _videoFrameQueue->pop();
        if (videoFrame) {
            NSLog(@"Video Frame: pts: %lld, duration: %lld", videoFrame->mPts, videoFrame->mDuration);
            int64_t pts = videoFrame->mPts;
            
            if (hasAudio) {
                // 有音频时使用原来的音频同步逻辑
                int64_t audioClock = _audioClock.load();
                int64_t diff = pts - audioClock;
                if (diff > 50) {
                    NSLog(@"Video frame pts %lld is more than audio clock %lld, waiting for audio", pts, audioClock);
                    [NSThread sleepForTimeInterval:diff / 1000.0];
                    auto frame = videoFrame.get();
                    [_videoRenderer renderVideoFrame:*frame];
//                    [_cvVideoRender renderVideoFrame:*frame];
                } else if (diff < -50) {
                    NSLog(@"Video frame pts %lld is less than audio clock %lld, skipping render", videoFrame->mPts, audioClock);
                    continue;
                } else {
                    auto frame = videoFrame.get();
                    [_videoRenderer renderVideoFrame:*frame];
//                    [_cvVideoRender renderVideoFrame:*frame];
                }
            } else {
                // 没有音频时使用帧持续时间
                auto frame = videoFrame.get();
                [_videoRenderer renderVideoFrame:*frame];
//                [_cvVideoRender renderVideoFrame:*frame];
                // 计算需要等待的时间
                int64_t frameDuration = videoFrame->mDuration;
                if (frameDuration <= 0) {
                    // 如果帧持续时间无效，使用默认值（例如 33ms，约 30fps）
                    frameDuration = 33;
                }
                
                // 等待适当的时间再处理下一帧
                [NSThread sleepForTimeInterval:frameDuration / 1000.0];
            }
        }
    }
}

@end
