#include "AppleLogger.h"
#include <Foundation/Foundation.h>

namespace yffplayer {

AppleLogger::AppleLogger() {
    // 构造函数，可以在这里进行初始化工作
}

AppleLogger::~AppleLogger() {
    // 析构函数，可以在这里进行清理工作
}

void AppleLogger::log(LogLevel level, const std::string& tag, const std::string& message) {
    // 将C++字符串转换为NSString
    NSString* nsTag = [NSString stringWithUTF8String:tag.c_str()];
    NSString* nsMessage = [NSString stringWithUTF8String:message.c_str()];
    NSString* nsLevel = [NSString stringWithUTF8String:logLevelToString(level)];
    
    // 使用NSLog输出日志
    NSLog(@"[%@][%@] %@", nsLevel, nsTag, nsMessage);
}

const char* AppleLogger::logLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Verbose:
            return "VERBOSE";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARNING";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

} // namespace yffplayer
