#pragma once

#include <thread>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace yffplayer {

class Decoder {
public:
    virtual ~Decoder() = default;

    virtual bool open(AVCodecParameters *codecParam, AVRational timeBase) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void flush() = 0;

protected:
    std::atomic<bool> mIsRunning{false};
    std::thread mDecodeThread;

    virtual void decodeLoop() = 0;
};

} // namespace yffplayer
