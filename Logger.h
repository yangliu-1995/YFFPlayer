#include <string>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace yffplayer {

    class Logger {
    public:
        enum class Level {
            VERBOSE,
            INFO,
            WARN,
            ERROR
        };

        virtual ~Logger() = default;

        // 子类只需要处理这些字段
        virtual void log(const char* levelStr, const char* timeStr, const char* fileNameOnly, int line, const char* fmt, va_list args) = 0;

    private:
        const char* extractFileName(const char* filePath) const {
            const char* p = strrchr(filePath, '/');
        #ifdef _WIN32
            const char* p2 = strrchr(filePath, '\\');
            if (!p || (p2 && p2 > p)) p = p2;
        #endif
            return p ? p + 1 : filePath;
        }

        const char* levelToString(Level level) const {
            switch (level) {
                case Level::VERBOSE: return "[VERBOSE]";
                case Level::INFO:    return "[INFO]";
                case Level::WARN:    return "[WARN]";
                case Level::ERROR:   return "[ERROR]";
                default:             return "[UNKNOWN]";
            }
        }

        std::string getCurrentTimeString() const {
            using namespace std::chrono;

            auto now = system_clock::now();
            auto time_t_now = system_clock::to_time_t(now);
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

            std::tm local_tm;
        #ifdef _WIN32
            localtime_s(&local_tm, &time_t_now);
        #else
            localtime_r(&time_t_now, &local_tm);
        #endif

            char buffer[64];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local_tm);

            // 获取当前时区偏移（非跨平台精确实现，但够用）
            char tzbuf[8] = "+00:00";
            std::strftime(tzbuf, sizeof(tzbuf), "%z", &local_tm);  // 输出类似 "+0800"
            std::string tzStr = std::string(tzbuf);
            if (tzStr.length() == 5) {
                tzStr.insert(3, ":");  // 变成 "+08:00"
            }

            std::ostringstream oss;
            oss << buffer << '.' << std::setw(3) << std::setfill('0') << ms.count() << tzStr;
            return oss.str();
        }

    public:
        void log(Level level, const char* fmt, const char* file = __builtin_FILE(), int line = __builtin_LINE(), ...) {
            const char* levelStr = levelToString(level);
            const char* filename = extractFileName(file);
            std::string timeStr = getCurrentTimeString();

            va_list args;
            va_start(args, line);
            log(levelStr, timeStr.c_str(), filename, line, fmt, args);
            va_end(args);
        }

        void verbose(const char* fmt, const char* file = __builtin_FILE(), int line = __builtin_LINE(), ...) {
            const char* levelStr = levelToString(Level::VERBOSE);
            const char* filename = extractFileName(file);
            std::string timeStr = getCurrentTimeString();
            va_list args;
            va_start(args, line);
            log(levelStr, timeStr.c_str(), filename, line, fmt, args);
            va_end(args);
        }

        void info(const char* fmt, const char* file = __builtin_FILE(), int line = __builtin_LINE(), ...) {
            const char* levelStr = levelToString(Level::INFO);
            const char* filename = extractFileName(file);
            std::string timeStr = getCurrentTimeString();
            va_list args;
            va_start(args, line);
            log(levelStr, timeStr.c_str(), filename, line, fmt, args);
            va_end(args);
        }

        void warn(const char* fmt, const char* file = __builtin_FILE(), int line = __builtin_LINE(), ...) {
            const char* levelStr = levelToString(Level::WARN);
            const char* filename = extractFileName(file);
            std::string timeStr = getCurrentTimeString();
            va_list args;
            va_start(args, line);
            log(levelStr, timeStr.c_str(), filename, line, fmt, args);
            va_end(args);
        }

        void error(const char* fmt, const char* file = __builtin_FILE(), int line = __builtin_LINE(), ...) {
            const char* levelStr = levelToString(Level::ERROR);
            const char* filename = extractFileName(file);
            std::string timeStr = getCurrentTimeString();
            va_list args;
            va_start(args, line);
            log(levelStr, timeStr.c_str(), filename, line, fmt, args);
            va_end(args);
        }
    };
} // namespace yffplayer
