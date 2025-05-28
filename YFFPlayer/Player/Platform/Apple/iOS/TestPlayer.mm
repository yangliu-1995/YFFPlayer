// TestPlayer.mm
#import "TestPlayer.h"
#include "Player.h"
#include "IOSAudioOutput.h"
#include "IOSVideoOutput.h"
#include "IOSAUAudioOutput.h"

@interface TestPlayer () {
    std::shared_ptr<yffplayer::Player> _player;
}
@end

@implementation TestPlayer

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView {
    self = [super init];
    if (self) {
        auto audioOutput = std::make_shared<IOSAudioOutput>();
        auto videoOutput = std::make_shared<IOSVideoOutput>(videoRenderView);
        _player = std::make_shared<yffplayer::Player>(audioOutput, videoOutput);
    }
    return self;
}

- (void)playVideoWithURL:(NSURL *)url {
    yffplayer::MediaInfo mediaInfo;
    if (_player->open(url.absoluteString.UTF8String, mediaInfo)) {
        NSLog(@"Media Info, hasAudio: %d, sampleRate: %d, channels: %d, hasVideo: %d, width: %d, height: %d, frameRate: %d",
              mediaInfo.mHasAudio, mediaInfo.mAudioSampleRate, mediaInfo.mAudioChannels,
              mediaInfo.mHasVideo, mediaInfo.mVideoWidth, mediaInfo.mVideoHeight, mediaInfo.mVideoFrameRate);
        _player->start();
    }
}

- (void)stop {
    _player->stop();
}

- (void)pause {
    _player->pause();
}

- (void)resume {
    _player->resume();
}

- (void)testSeek {
    _player->seek(120 * 1000);
}

@end
