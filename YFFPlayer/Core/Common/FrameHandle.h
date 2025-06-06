#pragma once

extern "C" {
#include <libavutil/frame.h>
}

namespace yffplayer {

/**
 * FrameHandle 是一个简单的包装器，用于在AudioDecoder和AudioProcessor之间传递AVFrame
 * 它只负责持有AVFrame指针，不进行任何数据转换
 */
class FrameHandle {
public:
    // 构造函数：接收AVFrame指针
    explicit FrameHandle(AVFrame* frame);

    // 析构函数：释放AVFrame资源
    ~FrameHandle();

    // 移动构造函数
    FrameHandle(FrameHandle&& other) noexcept;

    // 移动赋值操作符
    FrameHandle& operator=(FrameHandle&& other) noexcept;

    // 禁用拷贝构造函数和拷贝赋值操作符
    FrameHandle(const FrameHandle&) = delete;
    FrameHandle& operator=(const FrameHandle&) = delete;

    // 获取AVFrame指针
    AVFrame* getFrame() const { return frame_; }

    // 检查是否有效
    bool isValid() const { return frame_ != nullptr; }

private:
    AVFrame* frame_;
};

}  // namespace yffplayer