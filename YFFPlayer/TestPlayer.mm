//
//  TestPlayer.m
//  YFFPlayer
//
//  Created by Xueyuan Xiao on 2025/5/8.
//

#import "TestPlayer.h"

#include "Player.h"
#import "IOSAudioRenderer.h"
#import "IOSVideoRenderer.h"
#import "AppleLogger.h"

using namespace yffplayer;

class PlayerCallbackImpl : public PlayerCallback {
public:
    PlayerCallbackImpl(TestPlayer *player) : player(player) {}
    ~PlayerCallbackImpl() override = default;
    // 播放器状态回调
    void onPlayerStateChanged(PlayerState state) override {
    }

    // 播放进度回调
    void onPlaybackProgress(double position, double duration) override {
    }

    // 错误回调
    void onError(const Error& error) override {
    }

    // 媒体信息回调
    void onMediaInfo(const MediaInfo& info) override {
    }

    // 视频帧回调
    void onVideoFrame(VideoFrame& frame) override {
    }

    // 音频帧回调
    void onAudioFrame(AudioFrame& frame) override {
    }
private:
    __weak TestPlayer *player;
};

@interface TestPlayer () {
    std::shared_ptr<Player> _playerCore;
    std::shared_ptr<PlayerCallbackImpl> _playerCallback;
    std::shared_ptr<IOSAudioRenderer> _audioRenderer;
    std::shared_ptr<IOSVideoRenderer> _videoRenderer;
    std::shared_ptr<AppleLogger> _logger;
}
@end

@implementation TestPlayer

- (instancetype)init {
    self = [super init];
    _logger = std::make_shared<AppleLogger>();
    _playerCallback = std::make_shared<PlayerCallbackImpl>(self);
    _audioRenderer = std::make_shared<IOSAudioRenderer>();
    _videoRenderer = std::make_shared<IOSVideoRenderer>();
    _playerCore = std::make_shared<Player>(_playerCallback, _audioRenderer, _videoRenderer, _logger);
    return self;
}

- (void)start {
    NSURL *url = [[NSBundle mainBundle] URLForResource:@"m" withExtension:@"avi"];
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        _playerCore->open(url.absoluteString.UTF8String);
        _playerCore->start();
    });
}

@end
