#import "IOSCVPixelBufferVideoRenderer.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <CoreVideo/CoreVideo.h>

@interface IOSCVPixelBufferVideoRenderer () <MTKViewDelegate>
{
    MTKView *_mtkView;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    vector_uint2 _viewportSize;
}

// 线程安全访问 CVPixelBuffer
@property (nonatomic, strong) dispatch_queue_t pixelBufferQueue;
@property (nonatomic, assign) CVPixelBufferRef pixelBuffer;

@end

@implementation IOSCVPixelBufferVideoRenderer

- (instancetype)initWithView:(UIView *)view {
    self = [super init];
    if (self) {
        _pixelBufferQueue = dispatch_queue_create("com.yourapp.pixelbuffer.queue", DISPATCH_QUEUE_SERIAL);
        _device = MTLCreateSystemDefaultDevice();

        _mtkView = [[MTKView alloc] initWithFrame:view.bounds device:_device];
        _mtkView.delegate = self;
        _mtkView.preferredFramesPerSecond = 60;
        _mtkView.framebufferOnly = NO;
        _mtkView.contentMode = UIViewContentModeScaleAspectFit;
        _mtkView.translatesAutoresizingMaskIntoConstraints = NO;
        [view addSubview:_mtkView];
        [NSLayoutConstraint activateConstraints:@[
            [_mtkView.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
            [_mtkView.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
            [_mtkView.topAnchor constraintEqualToAnchor:view.topAnchor],
            [_mtkView.bottomAnchor constraintEqualToAnchor:view.bottomAnchor],
        ]];

        _commandQueue = [_device newCommandQueue];

        [self setupPipeline];
    }
    return self;
}

- (void)setupPipeline {
    id<MTLLibrary> defaultLibrary = [_device newDefaultLibrary];
    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"YUVToRGBPipeline";
    pipelineDescriptor.vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
    pipelineDescriptor.fragmentFunction = [defaultLibrary newFunctionWithName:@"yuvToRGBFragmentShader"];
    pipelineDescriptor.colorAttachments[0].pixelFormat = _mtkView.colorPixelFormat;

    NSError *error = nil;
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!_pipelineState) {
        NSLog(@"Failed to create pipeline state: %@", error);
    }
}

- (void)dealloc {
    if (_pixelBuffer) {
        CVPixelBufferRelease(_pixelBuffer);
        _pixelBuffer = NULL;
    }
}

#pragma mark - Public

- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame {
    if (frame.mFormat != yffplayer::PixelFormat::YUV420P) {
        NSLog(@"Only YUV420P format is supported");
        return;
    }

    dispatch_sync(_pixelBufferQueue, ^{
        [self createOrUpdatePixelBufferWithFrame:frame];
        _viewportSize = (vector_uint2){(uint32_t)CVPixelBufferGetWidth(self->_pixelBuffer),
                                      (uint32_t)CVPixelBufferGetHeight(self->_pixelBuffer)};
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_mtkView setNeedsDisplay];
        });
    });
}

#pragma mark - Helpers

- (void)createOrUpdatePixelBufferWithFrame:(const yffplayer::VideoFrame &)frame {
    int width = frame.mWidth;
    int height = frame.mHeight;

    NSDictionary *pixelBufferAttrs = @{
        (NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8Planar),
        (NSString*)kCVPixelBufferWidthKey : @(width),
        (NSString*)kCVPixelBufferHeightKey : @(height)
    };

    // 如果已有像素缓存且大小匹配，则直接更新，否则重新创建
    if (!_pixelBuffer ||
        CVPixelBufferGetWidth(_pixelBuffer) != width ||
        CVPixelBufferGetHeight(_pixelBuffer) != height) {
        if (_pixelBuffer) {
            CVPixelBufferRelease(_pixelBuffer);
            _pixelBuffer = NULL;
        }

        CVReturn ret = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                                           kCVPixelFormatType_420YpCbCr8Planar,
                                           (__bridge CFDictionaryRef)pixelBufferAttrs, &_pixelBuffer);
        if (ret != kCVReturnSuccess) {
            NSLog(@"Failed to create CVPixelBuffer");
            return;
        }
    }

    CVPixelBufferLockBaseAddress(_pixelBuffer, 0);

    // Y 平面
    uint8_t *baseAddressY = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(_pixelBuffer, 0);
    int bytesPerRowY = (int)CVPixelBufferGetBytesPerRowOfPlane(_pixelBuffer, 0);
    for (int i = 0; i < height; ++i) {
        memcpy(baseAddressY + i * bytesPerRowY, frame.mData.data() + i * frame.mLinesize[0], width);
    }

    // U 平面
    uint8_t *baseAddressU = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(_pixelBuffer, 1);
    int bytesPerRowU = (int)CVPixelBufferGetBytesPerRowOfPlane(_pixelBuffer, 1);
    int halfHeight = height / 2;
    int halfWidth = width / 2;
    size_t ySize = width * height;
    for (int i = 0; i < halfHeight; ++i) {
        memcpy(baseAddressU + i * bytesPerRowU, frame.mData.data() + ySize + i * frame.mLinesize[1], halfWidth);
    }

    // V 平面
    uint8_t *baseAddressV = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(_pixelBuffer, 2);
    int bytesPerRowV = (int)CVPixelBufferGetBytesPerRowOfPlane(_pixelBuffer, 2);
    size_t uSize = (width / 2) * (height / 2);
    for (int i = 0; i < halfHeight; ++i) {
        memcpy(baseAddressV + i * bytesPerRowV, frame.mData.data() + ySize + uSize + i * frame.mLinesize[2], halfWidth);
    }

    CVPixelBufferUnlockBaseAddress(_pixelBuffer, 0);
}

// 从 CVPixelBuffer 获取对应平面的 Metal 纹理
- (id<MTLTexture>)textureFromPixelBuffer:(CVPixelBufferRef)pixelBuffer planeIndex:(size_t)planeIndex {
    CVMetalTextureCacheRef textureCache = NULL;

    static dispatch_once_t onceToken;
    static CVMetalTextureCacheRef sharedCache = NULL;
    dispatch_once(&onceToken, ^{
        CVMetalTextureCacheCreate(NULL, NULL, _device, NULL, &sharedCache);
    });
    textureCache = sharedCache;

    if (!textureCache) {
        NSLog(@"Failed to create Metal texture cache");
        return nil;
    }

    size_t width = CVPixelBufferGetWidthOfPlane(pixelBuffer, planeIndex);
    size_t height = CVPixelBufferGetHeightOfPlane(pixelBuffer, planeIndex);
    OSType pixelFormatType = CVPixelBufferGetPixelFormatType(pixelBuffer);

    MTLPixelFormat pixelFormat = MTLPixelFormatR8Unorm;
    if (planeIndex == 0) {
        pixelFormat = MTLPixelFormatR8Unorm;
    } else if (planeIndex == 1 || planeIndex == 2) {
        pixelFormat = MTLPixelFormatR8Unorm;
    }

    CVMetalTextureRef textureRef = NULL;
    CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache,
                                                            pixelBuffer, NULL,
                                                            pixelFormat, width, height, planeIndex, &textureRef);
    if (ret != kCVReturnSuccess) {
        NSLog(@"Failed to create Metal texture from pixel buffer");
        return nil;
    }

    id<MTLTexture> texture = CVMetalTextureGetTexture(textureRef);
    CFRelease(textureRef);
    return texture;
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    _viewportSize = (vector_uint2){(uint32_t)size.width, (uint32_t)size.height};
}

- (void)drawInMTKView:(MTKView *)view {
    __block CVPixelBufferRef pixelBufferCopy = NULL;

    dispatch_sync(_pixelBufferQueue, ^{
        if (self->_pixelBuffer) {
            pixelBufferCopy = self->_pixelBuffer;
            CVPixelBufferRetain(pixelBufferCopy);
        }
    });

    if (!pixelBufferCopy) {
        return;
    }

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;
    if (!renderPassDescriptor) {
        [commandBuffer commit];
        CVPixelBufferRelease(pixelBufferCopy);
        return;
    }

    id<MTLTexture> textureY = [self textureFromPixelBuffer:pixelBufferCopy planeIndex:0];
    id<MTLTexture> textureU = [self textureFromPixelBuffer:pixelBufferCopy planeIndex:1];
    id<MTLTexture> textureV = [self textureFromPixelBuffer:pixelBufferCopy planeIndex:2];

    if (!textureY || !textureU || !textureV) {
        [commandBuffer commit];
        CVPixelBufferRelease(pixelBufferCopy);
        return;
    }

    id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    [renderEncoder setRenderPipelineState:_pipelineState];

    [renderEncoder setFragmentTexture:textureY atIndex:0];
    [renderEncoder setFragmentTexture:textureU atIndex:1];
    [renderEncoder setFragmentTexture:textureV atIndex:2];

    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:6];
    [renderEncoder endEncoding];

    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];

    CVPixelBufferRelease(pixelBufferCopy);
}

@end
