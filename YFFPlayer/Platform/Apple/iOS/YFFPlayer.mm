#import "YFFPlayer.h"
#include "Player.h"
#include "IOSAudioOutput.h"
#include "IOSVideoOutput.h"

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
    yffplayer::MediaInfo _mediaInfo;
}

@property (nonatomic, strong) UIView *videoRenderView;

@end

@implementation YFFPlayer

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView {
    self = [super init];
    if (self) {
        auto audioOutput = std::make_shared<IOSAudioOutput>();
        auto videoOutput = std::make_shared<IOSVideoOutput>(videoRenderView);
        auto playerCallback = std::make_shared<PlayerCallbackImp>(self);
        _player = std::make_shared<yffplayer::Player>(audioOutput, videoOutput, playerCallback);
    }
    return self;
}

- (void)playVideoWithURL:(NSURL *)url {
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        NSString *urlStr;
        if ([url isFileURL]) {
            urlStr = url.path;
        } else {
            urlStr = url.absoluteString;
        }
        if (self->_player->open(urlStr.UTF8String, self->_mediaInfo)) {
            self->_isLiveStream = self->_mediaInfo.isLiveStream_;
            NSLog(@"Media Info, hasAudio: %d, sampleRate: %d, channels: %d, hasVideo: %d, width: %d, height: %d, frameRate: %d",
                  self->_mediaInfo.hasAudio_, self->_mediaInfo.audioSampleRate_, self->_mediaInfo.audioChannels_,
                  self->_mediaInfo.hasVideo_, self->_mediaInfo.videoWidth_, self->_mediaInfo.videoHeight_, self->_mediaInfo.videoFrameRate_);
            self->_player->start();
        }
    });
}

- (void)stop {
    _player->stop();
    _player = nil;
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

- (void)seekTo:(float)position {
    if (_mediaInfo.isLiveStream_) {
        return;
    }
    double targetPosition = position * _mediaInfo.durationMs_;
    _player->seek(static_cast<int64_t>(targetPosition));
}

- (void)setPlaybackRate:(float)rate {
    _player->setPlaybackRate(rate);
}

- (void)onProgress:(int64_t)current total:(int64_t)total {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!self.progressHandler) {
            return;
        }
        self.progressHandler(current, total);
    });
}

@end
