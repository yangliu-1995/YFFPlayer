#include "FrameHandle.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace yffplayer {

FrameHandle::FrameHandle(AVFrame* frame) : mFrame(frame) {
    // 构造函数接收AVFrame指针，不进行拷贝
}

FrameHandle::~FrameHandle() {
    if (mFrame) {
        av_frame_free(&mFrame);
        mFrame = nullptr;
    }
}

FrameHandle::FrameHandle(FrameHandle&& other) noexcept : mFrame(other.mFrame) {
    other.mFrame = nullptr;
}

FrameHandle& FrameHandle::operator=(FrameHandle&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (mFrame) {
            av_frame_free(&mFrame);
        }
        
        // 移动资源
        mFrame = other.mFrame;
        other.mFrame = nullptr;
    }
    return *this;
}

}  // namespace yffplayer