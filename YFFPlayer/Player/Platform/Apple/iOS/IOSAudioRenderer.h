#pragma once

#include "AudioRenderer.h"
#include "AudioFrame.h"

#include <AudioToolbox/AudioToolbox.h>
#include <mutex>
#include <queue>

namespace yffplayer {

struct AudioFrame;

class IOSAudioRenderer : public AudioRenderer {
public:
    IOSAudioRenderer();
    ~IOSAudioRenderer() override;

    // 初始化渲染器
    bool init(int sampleRate, int channels, int bitsPerSample,
              std::shared_ptr<RendererCallback> callback) override;

    // 播放音频数据
    bool play(const AudioFrame& frame) override;

    // 暂停音频播放
    void pause() override;

    // 恢复音频播放
    void resume() override;

    // 停止音频播放
    void stop() override;

    // 设置音量
    void setVolume(float volume) override;

    // 获取音量
    float getVolume() const override;

    // 设置静音
    void setMute(bool mute) override;

    // 获取静音状态
    bool isMuted() const override;

    // 释放资源
    void release() override;

private:
    // AudioQueue 回调函数
    static void AudioQueueOutputCallback(void* inUserData,
                                        AudioQueueRef inAQ,
                                        AudioQueueBufferRef inBuffer);

    // 处理缓冲区播放完成
    void handleBufferCompleted(AudioQueueBufferRef buffer);

    // 填充音频缓冲区
    bool enqueueAudioFrame(const AudioFrame& frame);

    AudioQueueRef audioQueue_ = nullptr;
    std::shared_ptr<RendererCallback> callback_;
    mutable std::mutex mutex_;
    std::queue<AudioFrame> frameQueue_; // 存储待播放的音频帧
    bool isPlaying_ = false;
    bool isMuted_ = false;
    float volume_ = 1.0f;
    int sampleRate_ = 0;
    int channels_ = 0;
    int bitsPerSample_ = 0;
    bool isStarted_ = false;

    static constexpr int kNumBuffers = 3; // AudioQueue 缓冲区数量
    AudioQueueBufferRef buffers_[kNumBuffers];
};

} // namespace yffplayer
