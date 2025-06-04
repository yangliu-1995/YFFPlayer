#pragma once

namespace yffplayer {
class PlayerCallback {
public:
    virtual ~PlayerCallback() = default;
    virtual void onProgress(int64_t current, int64_t total) = 0;
    virtual void onCompleted() = 0;
};
}  // namespace yffplayer
