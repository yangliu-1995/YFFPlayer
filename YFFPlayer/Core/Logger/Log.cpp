#include "Log.h"

namespace yffplayer {
LogStream::LogStream(LogLevel level, const char* file, int line, Logger* logger) : mLevel(level), mFile(file), mLine(line), mLogger(logger) {}

LogStream::~LogStream() {
    std::ostringstream full;
    full << "[" << mFile << ":" << mLine << "] " << mStream.str();
    if (mLogger) {
        mLogger->log(mLevel, full.str());
    } else {
        const char* levelStr = "";
        switch (mLevel) {
            case LogLevel::Debug:   levelStr = "[DEBUG] "; break;
            case LogLevel::Info:    levelStr = "[INFO] "; break;
            case LogLevel::Warning: levelStr = "[WARNING] "; break;
            case LogLevel::Error:   levelStr = "[ERROR] "; break;
        }
        std::cerr << levelStr << full.str() << std::endl;
    }
}

LogStream& LogStream::operator<<(StreamManipulator manip) {
    manip(mStream);
    return *this;
}

void LogStream::setLogger(Logger* logger) { mLogger = logger; }

// Log

Logger* Log::logger_ = nullptr;
std::mutex Log::mutex_;

void Log::setLogger(Logger* logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    logger_ = logger;
}
}  // namespace yffplayer
