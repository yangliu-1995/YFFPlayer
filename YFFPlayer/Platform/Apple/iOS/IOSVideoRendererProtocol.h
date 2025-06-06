#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

namespace yffplayer {
class VideoFrame;
}

@protocol IOSVideoRendererProtocol <NSObject>

- (instancetype)initWithView:(UIView *)view;

- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame;
- (void)setFps:(NSInteger)fps;
- (void)setBrightness:(float)brightness;
- (float)brightness;
- (void)setContrast:(float)contrast;
- (float)contrast;
- (void)setSaturation:(float)saturation;
- (float)saturation;

@end
