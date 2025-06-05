#include "Log.h"

namespace yffplayer {
LogStream::LogStream(LogLevel level, Logger* logger)
: mLevel(level)
, mLogger(logger)
{}

LogStream::~LogStream() {
    if (mLogger) {
        mLogger->log(mLevel, mStream.str());
    } else {
        // 默认输出，带等级前缀
        const char* levelStr = "";
        switch(mLevel) {
            case LogLevel::Debug: levelStr = "[DEBUG] "; break;
            case LogLevel::Info: levelStr = "[INFO] "; break;
            case LogLevel::Warning: levelStr = "[WARNING] "; break;
            case LogLevel::Error: levelStr = "[ERROR] "; break;
        }
        std::cerr << levelStr << mStream.str() << std::flush;
    }
}

LogStream& LogStream::operator<<(StreamManipulator manip) {
    manip(mStream);
    return *this;
}

void LogStream::setLogger(Logger* logger) {
    mLogger = logger;
}

// Log

Logger* Log::logger_ = nullptr;
std::mutex Log::mutex_;

void Log::setLogger(Logger* logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    logger_ = logger;
}
} // namespace yffplayer
