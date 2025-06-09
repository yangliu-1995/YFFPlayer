#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#endif

typedef void(^PlayerProgressHandler)(NSInteger current, NSInteger duration);

@interface YFFPlayer : NSObject

@property(nonatomic, strong) PlayerProgressHandler progressHandler;

@property(nonatomic, readonly) BOOL isLiveStream;

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView;

- (void)playVideoWithURL:(NSURL *)url;

- (void)pause;

- (void)resume;

- (void)testSeek;

- (void)setPlaybackRate:(float)rate;

- (void)stop;

- (void)seekTo:(float)position;

@end

