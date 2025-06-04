#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

namespace yffplayer {

template <typename T>
class FrameQueue {
public:
    explicit FrameQueue(size_t capacity);

    void push(std::shared_ptr<T> frame);
    std::shared_ptr<T> pop();

    void clear();
    void abort();  // 中断阻塞操作
    void start();  // 恢复阻塞
    void flush();  // 等价于 abort + clear + start
    void wait_for_frames(size_t min_frames);
    
    size_t size() const;

private:
    std::queue<std::shared_ptr<T>> mQueue;
    size_t mCapacity;
    size_t mSize;

    mutable std::mutex mMutex;
    std::condition_variable mCondFull;
    std::condition_variable mCondEmpty;

    std::atomic<bool> mAborted;
};

}  // namespace yffplayer
