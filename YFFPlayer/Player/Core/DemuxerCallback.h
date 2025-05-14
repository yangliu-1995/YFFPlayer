#pragma once

#include "MediaInfo.h"
#include "PlayerTypes.h"

namespace yffplayer {

class DemuxerCallback {
   public:
    virtual ~DemuxerCallback() = default;

    // Notify demuxer state changes
    virtual void onDemuxerStateChanged(DemuxerState state) = 0;

    // Notify demuxer errors
    virtual void onDemuxerError(const Error& error) = 0;

    // Notify when media file reaches the end
    virtual void onEndOfFile() = 0;

    // Notify when media information is ready
    virtual void onMediaInfoReady(const MediaInfo& info) = 0;

    // Notify when seek operation is completed
    virtual void onSeekCompleted(int64_t position) = 0;
};

}  // namespace yffplayer
