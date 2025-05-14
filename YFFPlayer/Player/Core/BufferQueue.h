#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <stdexcept>

namespace yffplayer {
template <typename T>
class BufferQueue {
   private:
    std::queue<T> mQueue;
    mutable std::mutex mMutex;
    std::condition_variable mNotEmpty;
    std::condition_variable mNotFull;
    size_t mMaxSize;

   public:
    explicit BufferQueue(size_t maxSize = 100);

    void push(const T& item);
    T pop();
    bool tryPush(const T& item);
    bool tryPop(T& item);
    size_t size() const;
    bool empty() const;
    bool full() const;
    void clear();
};
}  // namespace yffplayer
