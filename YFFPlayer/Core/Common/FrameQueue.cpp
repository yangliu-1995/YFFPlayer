#include "FrameQueue.h"

#include "FrameHandle.h"

namespace yffplayer {

template <typename T>
FrameQueue<T>::FrameQueue(size_t capacity) : mCapacity(capacity), mSize(0), mAborted(false) {}

template <typename T>
void FrameQueue<T>::push(std::shared_ptr<T> frame) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondFull.wait(lock, [this]() { return mSize < mCapacity || mAborted.load(); });
    if (mAborted.load()) return;

    mQueue.push(std::move(frame));
    ++mSize;

    lock.unlock();
    mCondEmpty.notify_all();
}

template <typename T>
std::shared_ptr<T> FrameQueue<T>::pop() {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondEmpty.wait(lock, [this]() { return mSize > 0 || mAborted.load(); });
    if (mAborted.load()) return nullptr;

    auto frame = mQueue.front();
    mQueue.pop();
    --mSize;

    lock.unlock();
    mCondFull.notify_all();
    return frame;
}

template <typename T>
void FrameQueue<T>::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    while (!mQueue.empty()) {
        mQueue.pop();
    }
    mSize = 0;

    mCondFull.notify_all();
    mCondEmpty.notify_all();
}

template <typename T>
void FrameQueue<T>::abort() {
    mAborted.store(true);
    mCondFull.notify_all();
    mCondEmpty.notify_all();
}

template <typename T>
void FrameQueue<T>::start() {
    mAborted.store(false);
}

template <typename T>
void FrameQueue<T>::flush() {
    abort();
    clear();
    start();
}

template <typename T>
void FrameQueue<T>::wait_for_frames(size_t min_frames) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondEmpty.wait(lock, [this, min_frames]() { return mSize >= min_frames || mAborted.load(); });
}

template <typename T>
std::shared_ptr<T> FrameQueue<T>::back() const {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mQueue.empty()) {
        return nullptr;
    }
    return mQueue.front();
}

template <typename T>
size_t FrameQueue<T>::size() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSize;
}

// 显式实例化
template class FrameQueue<FrameHandle>;

}  // namespace yffplayer
