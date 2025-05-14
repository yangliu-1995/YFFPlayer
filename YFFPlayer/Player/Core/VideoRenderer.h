#pragma once

#include <cstdint>

#include "RendererCallback.h"

namespace yffplayer {
// Pixel format enumeration
enum class PixelFormat;
// Video frame structure
class VideoFrame;

// Video renderer interface
class VideoRenderer {
   public:
    virtual ~VideoRenderer() = default;

    // Initialize video renderer
    virtual bool init(int width, int height, PixelFormat format,
                      std::shared_ptr<RendererCallback> callback) = 0;

    // Render video frame
    virtual bool render(const VideoFrame& frame) = 0;

    // Release resources
    virtual void release() = 0;
};
}  // namespace yffplayer
