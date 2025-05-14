#pragma once
#include <memory>

namespace yffplayer {
// 音频帧结构体
class AudioFrame;
// 视频帧结构体
class VideoFrame;

// 视频渲染回调接口
class RendererCallback : public std::enable_shared_from_this<RendererCallback> {
   public:
    virtual ~RendererCallback() = default;

    // 通知播放器视频帧开始渲染，用于更新视频时钟
    virtual void onAudioFrameRendered(const AudioFrame& frame) = 0;

    // 通知播放器视频帧已渲染，用于更新视频时钟
    virtual void onVideoFrameRendered(const VideoFrame& frame) = 0;

    // 可扩展更多功能，例如渲染延迟通知、帧队列耗尽等
};

}  // namespace yffplayer
