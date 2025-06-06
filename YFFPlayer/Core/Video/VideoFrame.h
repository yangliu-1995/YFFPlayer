#pragma once

#include <cstdint>
#include <vector>

#define NUM_DATA_POINTERS 8

namespace yffplayer {

enum class PixelFormat { YUV420P, NV12, RGB24, VIDEOTOOLBOX };

struct VideoFrame {
    int64_t pts_ = 0;
    int64_t duration_ = 0;
    int width_ = 0;
    int height_ = 0;
    PixelFormat format_ = PixelFormat::YUV420P;
    bool isKeyFrame_ = false;
    int linesize_[NUM_DATA_POINTERS];

    void* frameHandle_ = nullptr;
    uint8_t* data_[NUM_DATA_POINTERS] = {nullptr};

    VideoFrame() = default;

    // 简化的构造函数，只接受句柄，内部解析所有信息
    explicit VideoFrame(void* frameHandle);

    ~VideoFrame();

    // 移动语义支持
    VideoFrame(VideoFrame&& other) noexcept;

    // 移动赋值操作符
    VideoFrame& operator=(VideoFrame&& other) noexcept;

    // 辅助方法
    uint8_t* getData(int plane) const;
    int getLinesize(int plane) const;
    bool isValid() const;

private:
    void setupDataPointers();
    void releaseFrameHandle(void* handle);

    // 禁用拷贝，避免复杂的引用计数管理
    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;
};

}  // namespace yffplayer
