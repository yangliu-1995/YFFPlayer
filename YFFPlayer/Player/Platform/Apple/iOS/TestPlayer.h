//
//  TestPlayer.h
//  TestPlayer
//
//  Created by Xueyuan Xiao on 2025/5/26.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

@interface TestPlayer : NSObject

@property (nonatomic, strong) UIView *videoRenderView;

- (instancetype)initWithVideoRenderView:(UIView *)videoRenderView;

- (void)playVideoWithURL:(NSURL *)url;

- (void)pause;

- (void)resume;

//- (void)play;

@end
