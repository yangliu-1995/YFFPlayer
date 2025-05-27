// FrameQueue.cpp（建议你使用 .tpp 或全头文件实现，见下）
#include "FrameQueue.h"
#include "AudioFrame.h"
#include "VideoFrame.h"

namespace yffplayer {

template<typename T>
FrameQueue<T>::FrameQueue(size_t capacity)
    : mCapacity(capacity), mSize(0), mAborted(false) {}

template<typename T>
void FrameQueue<T>::push(std::shared_ptr<T> frame) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondFull.wait(lock, [this]() {
        return mSize < mCapacity || mAborted.load();
    });
    if (mAborted.load()) return;

    mQueue.push(std::move(frame));
    ++mSize;

    lock.unlock();
    mCondEmpty.notify_one();
}

template<typename T>
std::shared_ptr<T> FrameQueue<T>::pop() {
    std::unique_lock<std::mutex> lock(mMutex);
    mCondEmpty.wait(lock, [this]() {
        return mSize > 0 || mAborted.load();
    });
    if (mAborted.load()) return nullptr;

    auto frame = mQueue.front();
    mQueue.pop();
    --mSize;

    lock.unlock();
    mCondFull.notify_one();
    return frame;
}

template<typename T>
void FrameQueue<T>::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    while (!mQueue.empty()) {
        mQueue.pop();
    }
    mSize = 0;

    mCondFull.notify_all();
    mCondEmpty.notify_all();
}

template<typename T>
void FrameQueue<T>::abort() {
    mAborted.store(true);
    mCondFull.notify_all();
    mCondEmpty.notify_all();
}

template<typename T>
void FrameQueue<T>::start() {
    mAborted.store(false);
}

template<typename T>
void FrameQueue<T>::flush() {
    abort();
    clear();
    start();
}

template<typename T>
size_t FrameQueue<T>::size() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mSize;
}

// 显式实例化
template class FrameQueue<AudioFrame>;
template class FrameQueue<VideoFrame>;

} // namespace yffplayer
