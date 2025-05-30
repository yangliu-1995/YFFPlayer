#pragma once
#import <UIKit/UIKit.h>
#import "IOSVideoRendererProtocol.h"
#include "VideoOutput.h"

class IOSVideoOutput : public yffplayer::VideoOutput {
public:
    IOSVideoOutput(UIView* view);
    ~IOSVideoOutput() override = default;

    bool initialize(int width, int height) override;
    void renderVideoFrame(const yffplayer::VideoFrame& frame) override;
    void stop() override;
    void pause() override;
    void resume() override;

private:
    id<IOSVideoRendererProtocol> mVideoRenderer;
};
