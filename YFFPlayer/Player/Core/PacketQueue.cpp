#include "PacketQueue.h"
#include "Packet.h"

namespace yffplayer {

PacketQueue::PacketQueue(size_t capacity)
    : mCapacity(capacity), mSize(0) {
}

bool PacketQueue::try_push(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mMutex);
    if (!mCondFull.wait_for(lock, timeout, [this]() { return mSize < mCapacity; })) {
        return false;
    }

    mQueue.push(std::move(packet));
    ++mSize;
    auto size = mSize;

    lock.unlock();
    if (size == 1) {
        mCondEmpty.notify_one();
    }
    return true;
}

void PacketQueue::push(std::shared_ptr<Packet> packet) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondFull.wait(lock, [this]() { return mSize < mCapacity; });

    mQueue.push(std::move(packet));
    ++mSize;
    auto size = mSize;

    lock.unlock();
    if (size == 1) {
        mCondEmpty.notify_one();
    }
}

std::shared_ptr<Packet> PacketQueue::pop() {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondEmpty.wait(lock, [this]() { return mSize > 0; });

    auto packet = mQueue.front();
    mQueue.pop();
    --mSize;
    auto size = mSize;

    lock.unlock();
    if (size == mCapacity - 1) {
        mCondFull.notify_one();
    }
    return packet;
}

size_t PacketQueue::size() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSize;
}

void PacketQueue::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    while (!mQueue.empty()) {
        auto packet = mQueue.front();
        mQueue.pop();
        // Packet 类的析构函数会自动释放 AVPacket
    }
    mSize = 0;

    mCondFull.notify_all();
    mCondEmpty.notify_all();
}

} // namespace yffplayer
