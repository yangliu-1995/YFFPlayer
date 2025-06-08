#include "IOSVideoOutput.h"
#include "VideoFrame.h"

#import "MetalVideoRenderer.h"

IOSVideoOutput::IOSVideoOutput(UIView* view) {
    mVideoRenderer = [[MetalVideoRenderer alloc] initWithView:view];
}

bool IOSVideoOutput::initialize(int width, int height, int fps) {
    [mVideoRenderer setFps: fps];
    return mVideoRenderer != nil;
}

void IOSVideoOutput::renderVideoFrame(const yffplayer::VideoFrame& frame) {
    [mVideoRenderer renderVideoFrame:frame];
}

void IOSVideoOutput::stop() {
}

void IOSVideoOutput::pause() {
}

void IOSVideoOutput::resume() {
}
