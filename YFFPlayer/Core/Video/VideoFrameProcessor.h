#pragma once

#include <memory>
#include <vector>

#include "FrameHandle.h"
#include "VideoFrame.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace yffplayer {
class VideoFrameProcessor {
public:
    VideoFrameProcessor() = default;
    ~VideoFrameProcessor();

    std::unique_ptr<VideoFrame> processAudioFrame(const std::shared_ptr<FrameHandle> frameHandle);

private:
    SwsContext* mSwsCtx = nullptr;
};
}  // namespace yffplayer
