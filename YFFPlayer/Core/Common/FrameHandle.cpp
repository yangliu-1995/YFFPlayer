#include "FrameHandle.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace yffplayer {

FrameHandle::FrameHandle(AVFrame* frame) : frame_(frame) {
    // 构造函数接收AVFrame指针，不进行拷贝
}

FrameHandle::~FrameHandle() {
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
}

FrameHandle::FrameHandle(FrameHandle&& other) noexcept : frame_(other.frame_) {
    other.frame_ = nullptr;
}

FrameHandle& FrameHandle::operator=(FrameHandle&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (frame_) {
            av_frame_free(&frame_);
        }

        // 移动资源
        frame_ = other.frame_;
        other.frame_ = nullptr;
    }
    return *this;
}

}  // namespace yffplayer
