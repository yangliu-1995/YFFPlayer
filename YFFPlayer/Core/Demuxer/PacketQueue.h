#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "Packet.h"

namespace yffplayer {

class PacketQueue {
public:
    PacketQueue(size_t capacity);

    bool try_push(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout);
    bool try_push_with_drop_if_keyframe(std::shared_ptr<Packet> packet,
                                        std::chrono::milliseconds timeout);
    void push(std::shared_ptr<Packet> packet);
    std::shared_ptr<Packet> pop();
    bool pop_last();

    void clear();  // 清空队列
    void abort();  // 中断阻塞操作
    void start();  // 恢复阻塞操作
    void flush();  // 等价于 clear + start
    size_t size() const;

private:
    std::queue<std::shared_ptr<Packet>> queue_;
    size_t capacity_;
    size_t size_;

    mutable std::mutex mutex_;
    std::condition_variable condFull_;
    std::condition_variable condEmpty_;

    std::atomic<bool> aborted_{false};
};

}  // namespace yffplayer
