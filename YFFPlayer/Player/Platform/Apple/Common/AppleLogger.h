#pragma once

#include "Logger.h"
#include <string>

namespace yffplayer {

class AppleLogger : public Logger {
public:
    AppleLogger();
    ~AppleLogger() override;

    // 实现Logger接口
    void log(LogLevel level, const std::string& tag, const std::string& message) override;

private:
    // 将LogLevel转换为字符串
    const char* logLevelToString(LogLevel level) const;
};

} // namespace yffplayer
