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

bool PacketQueue::pop_last() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mQueue.empty()) return false;

    std::queue<std::shared_ptr<Packet>> temp;
    while (mQueue.size() > 1) {
        temp.push(std::move(mQueue.front()));
        mQueue.pop();
    }

    // 丢弃最后一个
    mQueue.pop();
    --mSize;

    // 重新入队
    while (!temp.empty()) {
        mQueue.push(std::move(temp.front()));
        temp.pop();
    }

    mCondFull.notify_one();
    return true;
}


bool PacketQueue::try_push_with_drop_if_keyframe(std::shared_ptr<Packet> packet, std::chrono::milliseconds timeout) {
    if (try_push(packet, timeout)) {
        return true;
    }

    if (packet->isKeyFrame()) {
        if (pop_last()) {
            push(std::move(packet)); // 不需要阻塞了
            return true;
        }
    }

    return false; // 不是关键帧也不能推入
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
