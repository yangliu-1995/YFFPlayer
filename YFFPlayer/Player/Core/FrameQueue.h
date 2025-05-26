#pragma once

#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace yffplayer {

template<typename T>
class FrameQueue {
public:
    explicit FrameQueue(size_t capacity);

    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    void push(std::shared_ptr<T> frame);
    std::shared_ptr<T> pop();

    size_t size() const;
    void clear();

private:
    size_t mCapacity;
    mutable std::mutex mMutex;
    std::condition_variable mCondFull;
    std::condition_variable mCondEmpty;
    std::queue<std::shared_ptr<T>> mQueue;
    size_t mSize;
};

} // namespace yffplayer
