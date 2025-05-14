#pragma once

#include "RendererCallback.h"

#include <cstdint>
#include <memory>

namespace yffplayer {
struct AudioFrame;

class AudioRenderer {
   public:
    virtual ~AudioRenderer() = default;

    // 初始化渲染器
    virtual bool init(int sampleRate, int channels, int bitsPerSample,
                      std::shared_ptr<RendererCallback> callback) = 0;

    // 播放音频数据
    virtual bool play(const AudioFrame& frame) = 0;

    // 暂停音频播放
    virtual void pause() = 0;

    // 恢复音频播放
    virtual void resume() = 0;

    // 停止音频播放
    virtual void stop() = 0;

    // 设置音量
    virtual void setVolume(float volume) = 0;

    // 获取音量
    virtual float getVolume() const = 0;

    // 设置静音
    virtual void setMute(bool mute) = 0;

    // 获取静音状态
    virtual bool isMuted() const = 0;

    // 释放资源
    virtual void release() = 0;
};
}  // namespace yffplayer
