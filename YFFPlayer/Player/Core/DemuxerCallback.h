#pragma once

#include "MediaInfo.h"
#include "PlayerTypes.h"

namespace yffplayer {

class DemuxerCallback {
   public:
    virtual ~DemuxerCallback() = default;

    // 通知解复用器状态变化
    virtual void onDemuxerStateChanged(DemuxerState state) = 0;

    // 通知解复用器出错
    virtual void onDemuxerError(const Error& error) = 0;

    // 通知媒体文件已到结尾
    virtual void onEndOfFile() = 0;

    // 通知媒体信息已获取
    virtual void onMediaInfoReady(const MediaInfo& info) = 0;

    // 通知跳转操作完成
    virtual void onSeekCompleted(int64_t position) = 0;
};

}  // namespace yffplayer