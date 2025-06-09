#include "IOSVideoOutput.h"
#include "VideoFrame.h"
#include "Log.h"

#import "MetalVideoRenderer.h"

IOSVideoOutput::IOSVideoOutput(UIView* view) {
    videoRenderer_ = [[MetalVideoRenderer alloc] initWithView:view];
}

bool IOSVideoOutput::initialize(int width, int height, int fps) {
    [videoRenderer_ setFps: fps];
    return true;
}

void IOSVideoOutput::renderVideoFrame(const yffplayer::VideoFrame& frame) {
    [videoRenderer_ renderVideoFrame:frame];
}

void IOSVideoOutput::stop() {
}

void IOSVideoOutput::pause() {
}

void IOSVideoOutput::resume() {
}

IOSVideoOutput::~IOSVideoOutput() {
    [videoRenderer_ removeFromSuperview];
    videoRenderer_ = nil;
    LogInfo << "~IOSVideoOutput";
}
