#pragma once
#include "VideoOutput.h"
#import <UIKit/UIKit.h>
#import "IOSVideoRenderer.h"

namespace yffplayer {
class IOSVideoOutput : public VideoOutput {
public:
    IOSVideoOutput(UIView* view);
    ~IOSVideoOutput() override;

    bool initialize(int width, int height) override;
    void renderVideoFrame(const VideoFrame& frame) override;
    void stop() override;

private:
    IOSVideoRenderer* mVideoRenderer; // Objective-C 对象
};
} // namespace yffplayer
