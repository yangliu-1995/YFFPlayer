#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
typedef UIView PlatformView;
#elif TARGET_OS_OSX
#import <AppKit/AppKit.h>
typedef NSView PlatformView;
#endif
namespace yffplayer {
class VideoFrame;
}

@protocol VideoRendererProtocol <NSObject>

- (instancetype)initWithView:(PlatformView *)view;

- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame;
- (void)setFps:(NSInteger)fps;
- (void)setBrightness:(float)brightness;
- (float)brightness;
- (void)setContrast:(float)contrast;
- (float)contrast;
- (void)setSaturation:(float)saturation;
- (float)saturation;

@end
