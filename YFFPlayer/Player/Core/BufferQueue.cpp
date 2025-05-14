#include "BufferQueue.h"

#include "AudioFrame.h"
#include "VideoFrame.h"

extern "C" {
#include <libavcodec/packet.h>
}

namespace yffplayer {

template <typename T>
BufferQueue<T>::BufferQueue(size_t maxSize) : mMaxSize(maxSize) {
    if (maxSize == 0) {
        throw std::invalid_argument("Queue size must be greater than 0");
    }
}

template <typename T>
void BufferQueue<T>::push(const T& item) {
    std::unique_lock<std::mutex> lock(mMutex);
    mNotFull.wait(lock, [this]() { return mQueue.size() < mMaxSize; });

    mQueue.push(item);
    lock.unlock();
    mNotEmpty.notify_one();
}

template <typename T>
T BufferQueue<T>::pop() {
    std::unique_lock<std::mutex> lock(mMutex);
    mNotEmpty.wait(lock, [this]() { return !mQueue.empty(); });

    T item = mQueue.front();
    mQueue.pop();
    lock.unlock();
    mNotFull.notify_one();
    return item;
}

template <typename T>
bool BufferQueue<T>::tryPush(const T& item) {
    std::unique_lock<std::mutex> lock(mMutex, std::try_to_lock);
    if (!lock.owns_lock() || mQueue.size() >= mMaxSize) {
        return false;
    }

    mQueue.push(item);
    lock.unlock();
    mNotEmpty.notify_one();
    return true;
}

template <typename T>
bool BufferQueue<T>::tryPop(T& item) {
    std::unique_lock<std::mutex> lock(mMutex, std::try_to_lock);
    if (!lock.owns_lock() || mQueue.empty()) {
        return false;
    }

    item = mQueue.front();
    mQueue.pop();
    lock.unlock();
    mNotFull.notify_one();
    return true;
}

template <typename T>
size_t BufferQueue<T>::size() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue.size();
}

template <typename T>
bool BufferQueue<T>::empty() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue.empty();
}

template <typename T>
bool BufferQueue<T>::full() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue.size() >= mMaxSize;
}

template <typename T>
void BufferQueue<T>::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    while (!mQueue.empty()) {
        mQueue.pop();
    }
    mNotEmpty.notify_all();
    mNotFull.notify_all();
}

template class BufferQueue<std::shared_ptr<AudioFrame>>;
// 视频帧
template class BufferQueue<std::shared_ptr<VideoFrame>>;
// 数据包
template class BufferQueue<AVPacket*>;

}  // namespace yffplayer
