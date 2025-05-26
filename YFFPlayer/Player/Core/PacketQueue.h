#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace yffplayer {

struct Packet;

class PacketQueue {
public:
    explicit PacketQueue(size_t capacity);
    ~PacketQueue() = default;

    // 禁止拷贝和移动
    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;
    PacketQueue(PacketQueue&&) = delete;
    PacketQueue& operator=(PacketQueue&&) = delete;

    // 推入数据包（带超时）
    bool try_push(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout);
    // 推入数据包（可能阻塞）
    void push(std::shared_ptr<Packet> packet);
    // 弹出数据包（可能阻塞）
    std::shared_ptr<Packet> pop();
    // 获取当前队列大小
    size_t size() const;
    // 清空队列并释放所有数据包
    void clear();

private:
    std::queue<std::shared_ptr<Packet>> mQueue;
    mutable std::mutex mMutex;
    std::condition_variable mCondFull;
    std::condition_variable mCondEmpty;
    size_t mCapacity;
    size_t mSize;
};

} // namespace yffplayer
