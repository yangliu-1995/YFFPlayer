#pragma once

#include <cstdint>

#include "RendererCallback.h"

namespace yffplayer {
// 像素格式枚举
enum class PixelFormat;
// 视频帧结构体
class VideoFrame;

// 视频渲染器接口
class VideoRenderer {
   public:
    virtual ~VideoRenderer() = default;

    // 初始化视频渲染器
    virtual bool init(int width, int height, PixelFormat format,
                      std::shared_ptr<RendererCallback> callback) = 0;

    // 渲染视频帧
    virtual bool render(const VideoFrame& frame) = 0;

    // 释放资源
    virtual void release() = 0;
};
}  // namespace yffplayer
