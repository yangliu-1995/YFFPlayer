#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

#define LogDebug yffplayer::Log::Debug()
#define LogInfo yffplayer::Log::Info()
#define LogWarning yffplayer::Log::Warning()
#define LogError yffplayer::Log::Error()

namespace yffplayer {
enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
public:
    virtual ~Logger() = default;
    virtual void log(LogLevel level, const std::string& msg) = 0;
};

class LogStream {
public:
    LogStream(LogLevel level, Logger* logger = nullptr);
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
    std::ostringstream mStream;
    LogLevel mLevel;
    Logger* mLogger;
};

class Log {
public:
    static void setLogger(Logger* logger);

    static LogStream Debug() {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Debug, logger_);
    }

    static LogStream Info() {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Info, logger_);
    }

    static LogStream Warning() {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Warning, logger_);
    }

    static LogStream Error() {
        std::lock_guard<std::mutex> lock(mutex_);
        return LogStream(LogLevel::Info, logger_);
    }

private:
    static Logger* logger_;
    static std::mutex mutex_;
};
}  // namespace yffplayer
