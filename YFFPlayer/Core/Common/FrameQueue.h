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
    ~FrameQueue();

    void push(std::shared_ptr<T> frame);
    std::shared_ptr<T> pop();
    std::shared_ptr<T> back() const;

    void clear();
    void abort();  // 中断阻塞操作
    void start();  // 恢复阻塞
    void flush();  // 等价于 abort + clear + start
    void wait_for_frames(size_t min_frames);
    
    size_t size() const;

private:
    std::queue<std::shared_ptr<T>> queue_;
    size_t capacity_;
    size_t size_;

    mutable std::mutex mutex_;
    std::condition_variable condFull_;
    std::condition_variable condEmpty_;

    std::atomic<bool> aborted_;
};

}  // namespace yffplayer
