#pragma once

#include <memory>
#include <string>

#include "Error.h"

namespace yffplayer {

class Packet;

class DemuxerCallback {
public:
    virtual ~DemuxerCallback() = default;

    // 流状态回调
    virtual void onDemuxStarted() = 0;
    virtual void onDemuxPaused() = 0;
    virtual void onDemuxResumed() = 0;
    virtual void onDemuxStopped() = 0;

    // 错误和异常回调
    virtual void onReadError(const Error& error) = 0;
    virtual void onEndOfFile() = 0;
    virtual void onNetworkError(const Error& error) = 0;

    // 跳转相关回调
    virtual void onSeekStarted(int64_t targetTimestampMs) = 0;
    virtual void onSeekCompleted(int64_t actualTimestampMs) = 0;
    virtual void onSeekFailed(int64_t targetTimestampMs, const Error& error) = 0;

    // 进度回调
    virtual void onDemuxProgress(int64_t currentTimestampMs, int64_t durationMs) = 0;
};

}  // namespace yffplayer
