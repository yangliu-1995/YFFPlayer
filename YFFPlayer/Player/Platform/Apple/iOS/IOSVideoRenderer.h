#pragma once

#include "VideoFrame.h"
#include "VideoRenderer.h"

namespace yffplayer {
class IOSVideoRenderer: public VideoRenderer {
public:
    IOSVideoRenderer();
    ~IOSVideoRenderer() override;

    // Initialize renderer
    bool init(int width, int height, PixelFormat format, std::shared_ptr<RendererCallback> callback) override;

    // Render video frame
    bool render(const VideoFrame& frame) override;

    // Release resources
    void release() override;

private:
    int mWidth;          // Video width
    int mHeight;         // Video height
    PixelFormat mFormat; // Pixel format
    std::weak_ptr<RendererCallback> mCallback; // Callback interface
};
} // namespace yffplayer
