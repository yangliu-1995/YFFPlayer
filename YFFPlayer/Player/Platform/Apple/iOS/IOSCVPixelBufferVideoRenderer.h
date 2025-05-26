#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#import "VideoFrame.h"

@interface IOSCVPixelBufferVideoRenderer : NSObject

- (instancetype)initWithView:(UIView *)view;
- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame;

@end
