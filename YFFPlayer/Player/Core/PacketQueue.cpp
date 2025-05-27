#include "PacketQueue.h"

namespace yffplayer {

PacketQueue::PacketQueue(size_t capacity)
    : mCapacity(capacity), mSize(0) {}

bool PacketQueue::try_push(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mMutex);
    if (!mCondFull.wait_for(lock, timeout, [this]() {
        return mSize < mCapacity || mAborted.load();
    })) {
        return false;
    }
    if (mAborted.load()) return false;

    mQueue.push(std::move(packet));
    ++mSize;
    if (mSize == 1) {
        mCondEmpty.notify_one();
    }
    return true;
}

bool PacketQueue::pop_last() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mQueue.empty()) return false;

    std::queue<std::shared_ptr<Packet>> temp;
    while (mQueue.size() > 1) {
        temp.push(std::move(mQueue.front()));
        mQueue.pop();
    }
    mQueue.pop(); // 丢掉最后一个
    --mSize;
    while (!temp.empty()) {
        mQueue.push(std::move(temp.front()));
        temp.pop();
    }
    mCondFull.notify_one();
    return true;
}

bool PacketQueue::try_push_with_drop_if_keyframe(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout) {
    if (try_push(packet, timeout)) return true;
    if (packet->isKeyFrame() && pop_last()) {
        push(std::move(packet));
        return true;
    }
    return false;
}

void PacketQueue::push(std::shared_ptr<Packet> packet) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondFull.wait(lock, [this]() {
        return mSize < mCapacity || mAborted.load();
    });
    if (mAborted.load()) return;

    mQueue.push(std::move(packet));
    ++mSize;
    if (mSize == 1) {
        mCondEmpty.notify_one();
    }
}

std::shared_ptr<Packet> PacketQueue::pop() {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondEmpty.wait(lock, [this]() {
        return mSize > 0 || mAborted.load();
    });
    if (mAborted.load()) return nullptr;

    auto packet = mQueue.front();
    mQueue.pop();
    --mSize;
    if (mSize == mCapacity - 1) {
        mCondFull.notify_one();
    }
    return packet;
}

void PacketQueue::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    while (!mQueue.empty()) {
        mQueue.pop();
    }
    mSize = 0;
    mCondFull.notify_all();
    mCondEmpty.notify_all();
}

void PacketQueue::abort() {
    mAborted.store(true);
    mCondFull.notify_all();
    mCondEmpty.notify_all();
}

void PacketQueue::start() {
    mAborted.store(false);
}

void PacketQueue::flush() {
    abort();
    clear();
    start();
}

size_t PacketQueue::size() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSize;
}

} // namespace yffplayer
