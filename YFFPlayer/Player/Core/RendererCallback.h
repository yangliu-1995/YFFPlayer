#pragma once
#include <memory>

namespace yffplayer {
// Audio frame structure
class AudioFrame;
// Video frame structure
class VideoFrame;

// Video renderer callback interface
class RendererCallback : public std::enable_shared_from_this<RendererCallback> {
   public:
    virtual ~RendererCallback() = default;

    // Notify player when audio frame is rendered, used to update audio clock
    virtual void onAudioFrameRendered(const AudioFrame& frame) = 0;

    // Notify player when video frame is rendered, used to update video clock
    virtual void onVideoFrameRendered(const VideoFrame& frame) = 0;

    // Can be extended with more features, such as render delay notification, frame queue exhaustion, etc.
};

}  // namespace yffplayer
