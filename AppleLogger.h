#include "Logger.h"

namespace yffplayer {
class AppleLogger : public Logger {
public:
    void log(const char* level, const char* time, const char* file, int line, const char* fmt, va_list args) override;
};
} // namespace yffplayer

