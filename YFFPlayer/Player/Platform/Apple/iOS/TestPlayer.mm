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
}

@end

@implementation TestPlayer

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView {
    self = [super init];
    if (self) {
        _videoRenderView = videoRenderView;
        _audioQueue = std::make_shared<yffplayer::PacketQueue>(100);
        _videoQueue = std::make_shared<yffplayer::PacketQueue>(30);

        _audioFrameQueue = std::make_shared<yffplayer::FrameQueue<yffplayer::AudioFrame>>(100);
        _videoFrameQueue = std::make_shared<yffplayer::FrameQueue<yffplayer::VideoFrame>>(30);

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
    }
    return self;
}

- (void)playVideoWithURL:(NSURL *)url {
    yffplayer::MediaInfo mediaInfo;
    if (_demuxer->open(url.absoluteString.UTF8String, mediaInfo)) {
        NSLog(@"Media Info, hasAudio: %d, sampleRate: %d, channels: %d, hasVideo: %d, width: %d, height: %d",
              mediaInfo.mHasAudio, mediaInfo.mAudioSampleRate, mediaInfo.mAudiochannels,
              mediaInfo.mHasVideo, mediaInfo.mVideoWidth, mediaInfo.mVideoHeight);
        AVCodecParameters *audioCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(audioCodecParams, mediaInfo.mAudioCodecParameters);
        AVCodecParameters *videoCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(videoCodecParams, mediaInfo.mVideoCodecParameters);
        _audioDecoder = std::make_shared<yffplayer::AudioDecoder>(_audioQueue, _audioFrameQueue, audioCodecParams, mediaInfo.mAudioTimeBase);
        _videoDecoder = std::make_shared<yffplayer::VideoDecoder>(_videoQueue, _videoFrameQueue, videoCodecParams, mediaInfo.mVideoTimeBase);


        _demuxer->start();
        _audioDecoder->start();
        _videoDecoder->start();
        _audioRenderThread = [[NSThread alloc] initWithTarget:self selector:@selector(simAudioRenderThread) object:nil];
        _videoRenderThread = [[NSThread alloc] initWithTarget:self selector:@selector(simVideoRenderThread) object:nil];
        [_audioRenderThread start];
        [_videoRenderThread start];
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
    while (true) {
        auto videoFrame = _videoFrameQueue->pop();
        if (videoFrame) {
            NSLog(@"Video Frame: pts: %lld, duration: %lld", videoFrame->mPts, videoFrame->mDuration);
            int64_t audioClock = _audioClock.load();
            int64_t pts = videoFrame->mPts;
            int64_t diff = pts - audioClock;
            if (diff > 50) {
                NSLog(@"Video frame pts %lld is more than audio clock %lld, waiting for audio", pts, audioClock);
                [NSThread sleepForTimeInterval:diff / 1000.0];
                auto frame = videoFrame.get();
                [_videoRenderer renderVideoFrame:*frame];
            } else if (diff < -50) {
                NSLog(@"Video frame pts %lld is less than audio clock %lld, skipping render", videoFrame->mPts, audioClock);
                continue;
            } else {
                auto frame = videoFrame.get();
                [_videoRenderer renderVideoFrame:*frame];
            }
        }
    }
}

@end
