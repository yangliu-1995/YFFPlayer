#include "PacketQueue.h"

namespace yffplayer {

PacketQueue::PacketQueue(size_t capacity) : capacity_(capacity), size_(0) {}

bool PacketQueue::try_push(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!condFull_.wait_for(lock, timeout,
                            [this]() { return size_ < capacity_ || aborted_.load(); })) {
        return false;
    }
    if (aborted_.load()) return false;

    queue_.push(std::move(packet));
    ++size_;
    if (size_ == 1) {
        condEmpty_.notify_one();
    }
    return true;
}

bool PacketQueue::pop_last() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;

    std::queue<std::shared_ptr<Packet>> temp;
    while (queue_.size() > 1) {
        temp.push(std::move(queue_.front()));
        queue_.pop();
    }
    queue_.pop();  // 丢掉最后一个
    --size_;
    while (!temp.empty()) {
        queue_.push(std::move(temp.front()));
        temp.pop();
    }
    condFull_.notify_one();
    return true;
}

bool PacketQueue::try_push_with_drop_if_keyframe(std::shared_ptr<Packet> packet,
                                                 std::chrono::milliseconds timeout) {
    if (try_push(packet, timeout)) return true;
    if (packet->isKeyFrame() && pop_last()) {
        push(std::move(packet));
        return true;
    }
    return false;
}

void PacketQueue::push(std::shared_ptr<Packet> packet) {
    std::unique_lock<std::mutex> lock(mutex_);
    condFull_.wait(lock, [this]() { return size_ < capacity_ || aborted_.load(); });
    if (aborted_.load()) return;

    queue_.push(std::move(packet));
    ++size_;
    if (size_ == 1) {
        condEmpty_.notify_one();
    }
}

std::shared_ptr<Packet> PacketQueue::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    condEmpty_.wait(lock, [this]() { return size_ > 0 || aborted_.load(); });
    if (aborted_.load()) return nullptr;

    auto packet = queue_.front();
    queue_.pop();
    --size_;
    if (size_ == capacity_ - 1) {
        condFull_.notify_one();
    }
    return packet;
}

void PacketQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
    size_ = 0;
    condFull_.notify_all();
    condEmpty_.notify_all();
}

void PacketQueue::abort() {
    aborted_.store(true);
    condFull_.notify_all();
    condEmpty_.notify_all();
}

void PacketQueue::start() { aborted_.store(false); }

void PacketQueue::flush() {
    abort();
    clear();
    start();
}

size_t PacketQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

}  // namespace yffplayer
