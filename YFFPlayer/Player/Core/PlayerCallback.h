#pragma once

#include "PlayerTypes.h"

namespace yffplayer {
struct AudioFrame;
struct VideoFrame;
struct MediaInfo;

class PlayerCallback {
   public:
    virtual ~PlayerCallback() = default;

    // Player state callback
    virtual void onPlayerStateChanged(PlayerState state) = 0;

    // Playback progress callback
    virtual void onPlaybackProgress(double position, double duration) = 0;

    // Error callback
    virtual void onError(const Error& error) = 0;

    // Media info callback
    virtual void onMediaInfo(const MediaInfo& info) = 0;

    // Video frame callback
    virtual void onVideoFrame(VideoFrame& frame) = 0;

    // Audio frame callback
    virtual void onAudioFrame(AudioFrame& frame) = 0;
};

}  // namespace yffplayer
