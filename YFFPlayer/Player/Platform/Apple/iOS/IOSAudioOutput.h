#pragma once
#include "AudioOutput.h"
#include <Foundation/Foundation.h>
#include "IOSAudioRender.h"

namespace yffplayer {
class IOSAudioOutput : public AudioOutput {
public:
    IOSAudioOutput();
    ~IOSAudioOutput() override;

    bool initialize(int sampleRate, int channels, int frameBytes, std::shared_ptr<AudioOutputFrameProvider> frameProvider) override;
    void start() override;
    void feedAudioFrame(const AudioFrame& frame) override;
    void stop() override;

private:
    IOSAudioRender* mAudioRender; // Objective-C 对象
    std::weak_ptr<AudioOutputFrameProvider> mFrameProvider; // 弱引用，避免循环引用
};
} // namespace yffplayer
