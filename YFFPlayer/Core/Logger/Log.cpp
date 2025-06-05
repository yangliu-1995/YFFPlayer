#include "Log.h"

extern "C" {
#include <libavutil/log.h>
}

namespace yffplayer {
LogStream::LogStream(LogLevel level, const char* file, int line, Logger* logger)
    : mLevel(level), mFile(file), mLine(line), mLogger(logger) {}

LogStream::~LogStream() {
    std::ostringstream full;
    if (mLine >= 0) {
        full << mFile << ":" << mLine << " " << mStream.str();
    } else {
        full << mStream.str();
    }
    if (mLogger) {
        mLogger->log(mLevel, full.str());
    } else {
        const char* levelStr = "";
        switch (mLevel) {
            case LogLevel::Debug:
                levelStr = "[DEBUG] ";
                break;
            case LogLevel::Info:
                levelStr = "[INFO] ";
                break;
            case LogLevel::Warning:
                levelStr = "[WARNING] ";
                break;
            case LogLevel::Error:
                levelStr = "[ERROR] ";
                break;
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

static LogLevel mapFFmpegLevel(int avLevel) {
    if (avLevel <= AV_LOG_ERROR) return LogLevel::Error;
    if (avLevel <= AV_LOG_WARNING) return LogLevel::Warning;
    if (avLevel <= AV_LOG_INFO) return LogLevel::Info;
    return LogLevel::Debug;
}

void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl) {
    if (level > av_log_get_level()) {
        return;
    }

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, vl);

    // 清理结尾多余换行符（FFmpeg 会带 \n）
    std::string msg(buffer);
    if (!msg.empty() && msg.back() == '\n') {
        msg.pop_back();
    }

    LogLevel logLevel = mapFFmpegLevel(level);
    switch (logLevel) {
        case LogLevel::Debug:
            Log::Debug("", -1) << "[FFmpeg] " << msg;
            break;
        case LogLevel::Info:
            Log::Info("", -1) << "[FFmpeg] " << msg;
            break;
        case LogLevel::Warning:
            Log::Warning("", -1) << "[FFmpeg] " << msg;
            break;
        case LogLevel::Error:
            Log::Error("", -1) << "[FFmpeg] " << msg;
            break;
    }
}

void Log::redirectFFmpegLog() {
    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_callback(ffmpegLogCallback);
}
}  // namespace yffplayer
