#include "IOSVideoOutput.h"
#include "VideoFrame.h"

IOSVideoOutput::IOSVideoOutput(UIView* view) {
    mVideoRenderer = [[IOSCVPixelBufferVideoRenderer alloc] initWithView:view];
}

IOSVideoOutput::~IOSVideoOutput() {
}

bool IOSVideoOutput::initialize(int width, int height) {
    return mVideoRenderer != nil;
}

void IOSVideoOutput::renderVideoFrame(const yffplayer::VideoFrame& frame) {
    [mVideoRenderer renderVideoFrame:frame];
}

void IOSVideoOutput::stop() {
    // 如果需要停止逻辑，添加在此
}

void IOSVideoOutput::pause() {
}

void IOSVideoOutput::resume() {
}
