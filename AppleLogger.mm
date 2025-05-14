#import "AppleLogger.h"

#import <Foundation/Foundation.h>

namespace yffplayer {
void AppleLogger::log(const char* level, const char* time, const char* file, int line, const char* fmt, va_list args) {
    NSString* format = [NSString stringWithFormat:@"%s %s %s:%d: %s", time, level, file, line, fmt];
    NSString* output = [[NSString alloc] initWithFormat:format arguments:args];
    NSLog(@"%@", output);
}
}
