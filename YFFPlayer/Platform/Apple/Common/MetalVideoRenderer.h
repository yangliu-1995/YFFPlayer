#import <Foundation/Foundation.h>
#import "VideoRendererProtocol.h"

@interface MetalVideoRenderer : PlatformView <VideoRendererProtocol>

@property (nonatomic, assign) NSInteger fps;
@property (nonatomic, assign) float brightness;
@property (nonatomic, assign) float contrast;
@property (nonatomic, assign) float saturation;

@end
