
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "PlayerTypes.h"

extern "C" {
struct AVCodecParameters;
}

namespace yffplayer {

class Packet;
class AudioFrame;
class VideoFrame;
class Logger;

enum class DecoderType { AUDIO, VIDEO };

class Decoder {
   public:
    virtual ~Decoder() = default;

    virtual bool open(AVCodecParameters *codecParam) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;

   protected:
    std::shared_ptr<Logger> mLogger;
    DecoderType mType;
    std::atomic<bool> mIsRunning{false};
    std::thread mDecodeThread;

    virtual void decodeLoop() = 0;
};

}  // namespace yffplayer
