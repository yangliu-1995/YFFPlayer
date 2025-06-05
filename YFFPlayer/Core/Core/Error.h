#pragma once

#include <memory>
#include <string>

namespace yffplayer {
enum class ErrorCode {
    OK = 0,

    // 文件和网络错误
    FILE_OPEN_FAILED = 1001,
    STREAM_INFO_FAILED = 1002,
    NO_STREAMS_FOUND = 1003,

    // 读取错误
    READ_FRAME_FAILED = 2001,
    PACKET_ALLOCATION_FAILED = 2002,

    // 跳转错误
    SEEK_FAILED = 3001,
    SEEK_OUT_OF_RANGE = 3002,

    // 网络错误
    NETWORK_TIMEOUT = 4001,
    NETWORK_CONNECTION_FAILED = 4002,
    NETWORK_INTERRUPTED = 4003,

    // 内存错误
    OUT_OF_MEMORY = 5001,
    BUFFER_OVERFLOW = 5002,

    // 格式错误
    UNSUPPORTED_FORMAT = 6001,
    CORRUPTED_DATA = 6002,
};

struct Error {
    ErrorCode mCode;
    std::string mMessage;
    Error(ErrorCode code, const std::string& message) : mCode(code), mMessage(message){};
};
}  // namespace yffplayer