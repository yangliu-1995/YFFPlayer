#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

namespace yffplayer {
class VideoFrame;
}

@protocol IOSVideoRendererProtocol <NSObject>

- (instancetype)initWithView:(UIView *)view;
- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame;

@end
