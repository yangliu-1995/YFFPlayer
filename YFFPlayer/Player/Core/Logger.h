#pragma once

#include <string>

namespace yffplayer {

enum class LogLevel { Verbose, Info, Warning, Error };

class Logger {
   public:
    virtual ~Logger() = default;

    // 输出日志
    virtual void log(LogLevel level, const std::string& tag,
                     const std::string& message) = 0;
};

}  // namespace yffplayer
