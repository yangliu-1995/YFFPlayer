#pragma once

#include <memory>
#include <vector>

#include "VideoFrame.h"
#include "FrameHandle.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace yffplayer {
class VideoFrameProcessor{
public:
    VideoFrameProcessor() = default;
    ~VideoFrameProcessor();
    
    std::unique_ptr<VideoFrame> processAudioFrame(const std::shared_ptr<FrameHandle> frameHandle);
private:
    SwsContext* mSwsCtx = nullptr;
};
}  // namespace yffplayer
