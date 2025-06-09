#include "FrameQueue.h"
#include "Log.h"
#include "FrameHandle.h"

namespace yffplayer {

template <typename T>
FrameQueue<T>::FrameQueue(size_t capacity) : capacity_(capacity), size_(0), aborted_(false) {}

template <typename T>
void FrameQueue<T>::push(std::shared_ptr<T> frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    condFull_.wait(lock, [this]() { return size_ < capacity_ || aborted_.load(); });
    if (aborted_.load()) return;

    queue_.push(std::move(frame));
    ++size_;

    lock.unlock();
    condEmpty_.notify_all();
}

template <typename T>
std::shared_ptr<T> FrameQueue<T>::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    condEmpty_.wait(lock, [this]() { return size_ > 0 || aborted_.load(); });
    if (aborted_.load()) return nullptr;

    auto frame = queue_.front();
    queue_.pop();
    --size_;

    lock.unlock();
    condFull_.notify_all();
    return frame;
}

template <typename T>
void FrameQueue<T>::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
    size_ = 0;

    condFull_.notify_all();
    condEmpty_.notify_all();
}

template <typename T>
void FrameQueue<T>::abort() {
    aborted_.store(true);
    condFull_.notify_all();
    condEmpty_.notify_all();
}

template <typename T>
void FrameQueue<T>::start() {
    aborted_.store(false);
}

template <typename T>
void FrameQueue<T>::flush() {
    abort();
    clear();
    start();
}

template <typename T>
void FrameQueue<T>::wait_for_frames(size_t min_frames) {
    std::unique_lock<std::mutex> lock(mutex_);
    condEmpty_.wait(lock, [this, min_frames]() { return size_ >= min_frames || aborted_.load(); });
}

template <typename T>
std::shared_ptr<T> FrameQueue<T>::back() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return nullptr;
    }
    return queue_.front();
}

template <typename T>
size_t FrameQueue<T>::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

template <typename T>
FrameQueue<T>::~FrameQueue() {
    clear();
    LogInfo << "~FrameQueue";
}

// 显式实例化
template class FrameQueue<FrameHandle>;

}  // namespace yffplayer
