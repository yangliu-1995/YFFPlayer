#import "YFFPlayer.h"
#include "Player.h"
#include "IOSAudioOutput.h"
#include "IOSVideoOutput.h"
#include "IOSAUAudioOutput.h"

@protocol PlayerCoreDelegate <NSObject>

- (void)onProgress:(int64_t)current total:(int64_t)total;

@end

class PlayerCallbackImp: public yffplayer::PlayerCallback {
public:
    PlayerCallbackImp(id<PlayerCoreDelegate> delegate): mDelegate(delegate) {}
    void onProgress(int64_t current, int64_t total) override {
        if (!mDelegate) {
            return;
        }
        [mDelegate onProgress:current total:total];
    }
    void onCompleted() override {

    }
private:
    __weak id<PlayerCoreDelegate> mDelegate;
};

@interface YFFPlayer ()<PlayerCoreDelegate> {
    std::shared_ptr<yffplayer::Player> _player;
}
@end

@implementation YFFPlayer

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView {
    self = [super init];
    if (self) {
        auto audioOutput = std::make_shared<IOSAUAudioOutput>();
        auto videoOutput = std::make_shared<IOSVideoOutput>(videoRenderView);
        auto playerCallback = std::make_shared<PlayerCallbackImp>(self);
        _player = std::make_shared<yffplayer::Player>(audioOutput, videoOutput, playerCallback);
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
//    _player->seek(120 * 1000);
    _player->setPlaybackRate(2);
}

- (void)onProgress:(int64_t)current total:(int64_t)total {
//    if (total == 0) {
//        return;
//    }
//    NSLog(@"progress: %f", ((double)current) / (((double)total)));
}

@end
