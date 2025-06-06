#include "IOSVideoOutput.h"
#include "VideoFrame.h"

#import "IOSMTKVideoRenderer.h"

IOSVideoOutput::IOSVideoOutput(UIView* view) {
    mVideoRenderer = [[IOSMTKVideoRenderer alloc] initWithView:view];
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
