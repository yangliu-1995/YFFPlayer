#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#import "VideoFrame.h"

NS_ASSUME_NONNULL_BEGIN

@interface IOSVideoRenderer : NSObject

- (instancetype)initWithView:(UIView *)view;
- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame;

@end

NS_ASSUME_NONNULL_END
