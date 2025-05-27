#pragma once
#include <memory>
#include "AudioFrame.h"

namespace yffplayer {
class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    virtual bool init(int sampleRate, int channels) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;

    // 阻塞式推送音频帧，必要时等待缓冲空间
    virtual bool enqueueAudioFrame(const AudioFrame& frame) = 0;
};
}
