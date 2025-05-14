#include "IOSVideoRenderer.h"

#import <Foundation/Foundation.h>

namespace yffplayer {
IOSVideoRenderer::IOSVideoRenderer() {
    // Initialize the renderer
}

IOSVideoRenderer::~IOSVideoRenderer() {
    // Clean up the renderer
}

bool IOSVideoRenderer::init(int width, int height, PixelFormat format, std::shared_ptr<RendererCallback> callback) {
    mWidth = width;
    mHeight = height;
    mFormat = format;
    mCallback = callback;
    return true;
}

bool IOSVideoRenderer::render(const VideoFrame& frame) {
    // Render the video frame
    // This is where you would implement the rendering logic using iOS APIs
    auto callback = mCallback.lock();
    if (callback) {
        callback->onVideoFrameRendered(frame);
    }
    NSLog(@"视频帧渲染完成，pts: %lld", frame.pts);
    return true;
}

void IOSVideoRenderer::release() {
    // Release any resources held by the renderer
}
};
