#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define LogDebug yffplayer::Log::Debug(__FILENAME__, __LINE__)
#define LogInfo yffplayer::Log::Info(__FILENAME__, __LINE__)
#define LogWarning yffplayer::Log::Warning(__FILENAME__, __LINE__)
#define LogError yffplayer::Log::Error(__FILENAME__, __LINE__)

namespace yffplayer {
enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
public:
    virtual ~Logger() = default;
    virtual void log(LogLevel level, const std::string& msg) = 0;
};

class LogStream {
public:
    LogStream(LogLevel level, const char* file, int line, Logger* logger = nullptr);
    ~LogStream();

    template <typename T>
    LogStream& operator<<(const T& value) {
        mStream << value;
        return *this;
    }

    using StreamManipulator = std::ostream& (*)(std::ostream&);
    LogStream& operator<<(StreamManipulator manip);

    void setLogger(Logger* logger);

private:
    LogLevel mLevel;
    std::ostringstream mStream;
    const char* mFile;
    int mLine;
    Logger* mLogger;
};

class Log {
public:
    static void redirectFFmpegLog();
    static void setLogger(Logger* logger);

    static LogStream Debug(const char* file, int line) {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Debug, file, line, logger_);
    }

    static LogStream Info(const char* file, int line) {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Info, file, line, logger_);
    }

    static LogStream Warning(const char* file, int line) {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Warning, file, line, logger_);
    }

    static LogStream Error(const char* file, int line) {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Error, file, line, logger_);
    }

private:
    static Logger* logger_;
    static std::mutex mutex_;
};
}  // namespace yffplayer
