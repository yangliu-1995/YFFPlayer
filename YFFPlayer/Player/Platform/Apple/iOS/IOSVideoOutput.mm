#include "IOSVideoOutput.h"
#include "VideoFrame.h"

namespace yffplayer {
IOSVideoOutput::IOSVideoOutput(UIView* view) {
    mVideoRenderer = [[IOSVideoRenderer alloc] initWithView:view];
}

IOSVideoOutput::~IOSVideoOutput() {
}

bool IOSVideoOutput::initialize(int width, int height) {
    return mVideoRenderer != nil;
}

void IOSVideoOutput::renderVideoFrame(const VideoFrame& frame) {
    [mVideoRenderer renderVideoFrame:frame];
}

void IOSVideoOutput::stop() {
    // 如果需要停止逻辑，添加在此
}
} // namespace yffplayer
