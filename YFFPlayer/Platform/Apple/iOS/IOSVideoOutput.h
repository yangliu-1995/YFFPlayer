#pragma once
#import <UIKit/UIKit.h>
#import "VideoRendererProtocol.h"
#include "VideoOutput.h"

class IOSVideoOutput : public yffplayer::VideoOutput {
public:
    IOSVideoOutput(UIView* view);
    ~IOSVideoOutput() override = default;

    bool initialize(int width, int height, int fps) override;
    void renderVideoFrame(const yffplayer::VideoFrame& frame) override;
    void stop() override;
    void pause() override;
    void resume() override;

private:
    id<VideoRendererProtocol> videoRenderer_{nullptr};
};
