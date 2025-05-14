#pragma once

#include "PlayerTypes.h"

namespace yffplayer {
struct AudioFrame;
struct VideoFrame;
struct MediaInfo;

class PlayerCallback {
   public:
    virtual ~PlayerCallback() = default;

    // 播放器状态回调
    virtual void onPlayerStateChanged(PlayerState state) = 0;

    // 播放进度回调
    virtual void onPlaybackProgress(double position, double duration) = 0;

    // 错误回调
    virtual void onError(const Error& error) = 0;

    // 媒体信息回调
    virtual void onMediaInfo(const MediaInfo& info) = 0;

    // 视频帧回调
    virtual void onVideoFrame(VideoFrame& frame) = 0;

    // 音频帧回调
    virtual void onAudioFrame(AudioFrame& frame) = 0;
};

}  // namespace yffplayer
