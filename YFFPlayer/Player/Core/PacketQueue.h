#pragma once

#include "Packet.h"
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

namespace yffplayer {

class PacketQueue {
public:
    PacketQueue(size_t capacity);

    bool try_push(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout);
    bool try_push_with_drop_if_keyframe(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout);
    void push(std::shared_ptr<Packet> packet);
    std::shared_ptr<Packet> pop();
    bool pop_last();

    void clear();   // 清空队列
    void abort();   // 中断阻塞操作
    void start();   // 恢复阻塞操作
    void flush();   // 等价于 clear + start
    size_t size() const;

private:
    std::queue<std::shared_ptr<Packet>> mQueue;
    size_t mCapacity;
    size_t mSize;

    mutable std::mutex mMutex;
    std::condition_variable mCondFull;
    std::condition_variable mCondEmpty;

    std::atomic<bool> mAborted {false};
};

} // namespace yffplayer
