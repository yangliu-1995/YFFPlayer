#pragma once
#include <memory>

namespace yffplayer {
class AudioFrame;
class AudioOutputFrameProvider {
public:
    virtual ~AudioOutputFrameProvider() = default;
    // 获取音频帧
    virtual std::shared_ptr<AudioFrame> getNextAudioFrame() = 0;

};
} // namespace yffplayer
