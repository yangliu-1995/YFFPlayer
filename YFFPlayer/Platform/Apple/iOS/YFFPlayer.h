#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>

@interface YFFPlayer : NSObject

@property (nonatomic, strong) UIView *videoRenderView;

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView;

- (void)playVideoWithURL:(NSURL *)url;

- (void)pause;

- (void)resume;

- (void)testSeek;

- (void)setPlaybackRate:(float)rate;

//- (void)play;

@end
#endif
