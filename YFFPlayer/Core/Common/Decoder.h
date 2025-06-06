#pragma once

#include <atomic>
#include <thread>

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
    std::atomic<bool> isRunning_{false};
    std::thread decodeThread_;

    virtual void decodeLoop() = 0;
};

}  // namespace yffplayer
