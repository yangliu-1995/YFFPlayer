#pragma once
#include <memory>

namespace yffplayer {
class VideoFrame;

class VideoOutput {
public:
    virtual ~VideoOutput() = default;
    virtual bool initialize(int width, int height) = 0;
    virtual void renderVideoFrame(const VideoFrame& frame) = 0;
    virtual void stop() = 0;
};
} // namespace yffplayer
