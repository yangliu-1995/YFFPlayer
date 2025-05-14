#pragma once

#include "VideoFrame.h"
#include "VideoRenderer.h"

namespace yffplayer {
class IOSVideoRenderer: public VideoRenderer {
public:
    IOSVideoRenderer();
    ~IOSVideoRenderer() override;

    // 初始化渲染器
    bool init(int width, int height, PixelFormat format, std::shared_ptr<RendererCallback> callback) override;

    // 播放视频数据
    bool render(const VideoFrame& frame) override;

    // 释放资源
    void release() override;

private:
    int mWidth; // 视频宽度
    int mHeight; // 视频高度
    PixelFormat mFormat; // 像素格式
    std::weak_ptr<RendererCallback> mCallback; // 回调接口
};
} // namespace yffplayer
