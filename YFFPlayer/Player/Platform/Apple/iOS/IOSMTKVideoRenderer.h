#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>
#import "IOSVideoRendererProtocol.h"

@interface IOSMTKVideoRenderer : NSObject <IOSVideoRendererProtocol, MTKViewDelegate>

@property (nonatomic, assign) float brightness;
@property (nonatomic, assign) float contrast;
@property (nonatomic, assign) float saturation;

@end