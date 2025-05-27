#pragma once
#include "VideoOutput.h"
#import <UIKit/UIKit.h>
#import "IOSVideoRenderer.h"

class IOSVideoOutput : public yffplayer::VideoOutput {
public:
    IOSVideoOutput(UIView* view);
    ~IOSVideoOutput() override;

    bool initialize(int width, int height) override;
    void renderVideoFrame(const yffplayer::VideoFrame& frame) override;
    void stop() override;
    void pause() override;
    void resume() override;
    
private:
    IOSVideoRenderer* mVideoRenderer; // Objective-C 对象
};
