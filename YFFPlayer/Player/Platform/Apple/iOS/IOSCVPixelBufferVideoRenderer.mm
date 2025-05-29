#import "IOSCVPixelBufferVideoRenderer.h"
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

@interface IOSCVPixelBufferVideoRenderer () <MTKViewDelegate> {
    MTKView *_mtkView;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    dispatch_queue_t _pixelBufferQueue;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLRenderPipelineState> _nv12PipelineState;
    vector_uint2 _viewportSize;
}

// 线程安全访问 CVPixelBuffer
@property(nonatomic, strong) dispatch_queue_t pixelBufferQueue;
@property(nonatomic, assign) CVPixelBufferRef pixelBuffer;

@end

@implementation IOSCVPixelBufferVideoRenderer

- (instancetype)initWithView:(UIView *)view {
    self = [super init];
    if (self) {
        _pixelBufferQueue =
            dispatch_queue_create("com.yourapp.pixelbuffer.queue", DISPATCH_QUEUE_SERIAL);
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

        [self setupMetal]; // 改为调用 setupMetal
    }
    
    return self;
}

- (void)setupPipeline {
    id<MTLLibrary> defaultLibrary = [_device newDefaultLibrary];
    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"YUVToRGBPipeline";
    pipelineDescriptor.vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
    pipelineDescriptor.fragmentFunction =
        [defaultLibrary newFunctionWithName:@"yuvToRGBFragmentShader"];
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

    // 如果是 VideoToolbox 格式，直接使用传入的 CVPixelBufferRef
    if (frame.mFormat == yffplayer::PixelFormat::VIDEOTOOLBOX) {
        if (frame.mPixelBuffer) {
            // 释放旧的 pixelBuffer
            if (_pixelBuffer) {
                CVPixelBufferRelease(_pixelBuffer);
            }
            // 直接使用 VideoToolbox 解码的 CVPixelBufferRef
            _pixelBuffer = frame.mPixelBuffer;
            CFRetain(_pixelBuffer); // 增加引用计数
        }
        return;
    }

    // 原有的软件解码逻辑保持不变
    NSDictionary *pixelBufferAttrs = @{
        (NSString *)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8Planar),
        (NSString *)kCVPixelBufferWidthKey : @(width),
        (NSString *)kCVPixelBufferHeightKey : @(height)
    };

    // 如果已有像素缓存且大小匹配，则直接更新，否则重新创建
    if (!_pixelBuffer || CVPixelBufferGetWidth(_pixelBuffer) != width ||
        CVPixelBufferGetHeight(_pixelBuffer) != height) {
        if (_pixelBuffer) {
            CVPixelBufferRelease(_pixelBuffer);
            _pixelBuffer = NULL;
        }

        CVReturn ret = CVPixelBufferCreate(
            kCFAllocatorDefault, width, height, kCVPixelFormatType_420YpCbCr8Planar,
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
        memcpy(baseAddressU + i * bytesPerRowU, frame.mData.data() + ySize + i * frame.mLinesize[1],
               halfWidth);
    }

    // V 平面
    uint8_t *baseAddressV = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(_pixelBuffer, 2);
    int bytesPerRowV = (int)CVPixelBufferGetBytesPerRowOfPlane(_pixelBuffer, 2);
    size_t uSize = (width / 2) * (height / 2);
    for (int i = 0; i < halfHeight; ++i) {
        memcpy(baseAddressV + i * bytesPerRowV,
               frame.mData.data() + ySize + uSize + i * frame.mLinesize[2], halfWidth);
    }

    CVPixelBufferUnlockBaseAddress(_pixelBuffer, 0);
}

// 从 CVPixelBuffer 获取对应平面的 Metal 纹理
- (id<MTLTexture>)textureFromPixelBuffer:(CVPixelBufferRef)pixelBuffer
                              planeIndex:(size_t)planeIndex {
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

    // 获取像素格式和平面数量
    OSType pixelFormatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    size_t planeCount = CVPixelBufferGetPlaneCount(pixelBuffer);
    
    // 检查平面索引是否有效
    if (planeIndex >= planeCount) {
        NSLog(@"Invalid plane index %zu for pixel format %d with %zu planes", 
              planeIndex, pixelFormatType, planeCount);
        return nil;
    }

    size_t width = CVPixelBufferGetWidthOfPlane(pixelBuffer, planeIndex);
    size_t height = CVPixelBufferGetHeightOfPlane(pixelBuffer, planeIndex);

    // 根据像素格式和平面索引确定 Metal 像素格式
    MTLPixelFormat pixelFormat = MTLPixelFormatR8Unorm;
    
    switch (pixelFormatType) {
        case kCVPixelFormatType_420YpCbCr8Planar: // YUV420P (3 planes)
            pixelFormat = MTLPixelFormatR8Unorm; // Y, U, V 都是单通道
            break;
            
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: // NV12 (2 planes)
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
            if (planeIndex == 0) {
                pixelFormat = MTLPixelFormatR8Unorm; // Y plane
            } else if (planeIndex == 1) {
                pixelFormat = MTLPixelFormatRG8Unorm; // UV plane (interleaved)
            }
            break;
            
        case kCVPixelFormatType_32BGRA:
            pixelFormat = MTLPixelFormatBGRA8Unorm;
            break;
            
        default:
            NSLog(@"Unsupported pixel format: %d", pixelFormatType);
            return nil;
    }

    CVMetalTextureRef textureRef = NULL;
    CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, textureCache,
                                                             pixelBuffer, NULL, pixelFormat, width,
                                                             height, planeIndex, &textureRef);
    if (ret != kCVReturnSuccess) {
        NSLog(@"Failed to create Metal texture from pixel buffer, error: %d", ret);
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

    // 根据像素格式创建相应的纹理
    OSType pixelFormatType = CVPixelBufferGetPixelFormatType(pixelBufferCopy);
    
    id<MTLTexture> textureY = [self textureFromPixelBuffer:pixelBufferCopy planeIndex:0];
    id<MTLTexture> textureU = nil;
    id<MTLTexture> textureV = nil;
    
    if (pixelFormatType == kCVPixelFormatType_420YpCbCr8Planar) {
        // YUV420P: 3个分离平面
        textureU = [self textureFromPixelBuffer:pixelBufferCopy planeIndex:1]; // U plane
        textureV = [self textureFromPixelBuffer:pixelBufferCopy planeIndex:2]; // V plane
    } else if (pixelFormatType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
               pixelFormatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
        // NV12: 2个平面，第二个平面是UV交错
        textureU = [self textureFromPixelBuffer:pixelBufferCopy planeIndex:1]; // UV plane
        // 对于NV12，需要使用不同的shader
    }

    if (!textureY || (pixelFormatType == kCVPixelFormatType_420YpCbCr8Planar && (!textureU || !textureV))) {
        [commandBuffer commit];
        CVPixelBufferRelease(pixelBufferCopy);
        return;
    }

    // 创建render encoder
    id<MTLRenderCommandEncoder> renderEncoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    
    // 根据格式选择pipeline
    id<MTLRenderPipelineState> pipelineToUse;
    if (pixelFormatType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
        pixelFormatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
        pipelineToUse = _nv12PipelineState; // 使用NV12 shader
    } else {
        pipelineToUse = _pipelineState; // 使用YUV420P shader
    }
    
    [renderEncoder setRenderPipelineState:pipelineToUse];

    [renderEncoder setFragmentTexture:textureY atIndex:0];
    if (textureU) [renderEncoder setFragmentTexture:textureU atIndex:1];
    if (textureV) [renderEncoder setFragmentTexture:textureV atIndex:2];

    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:6];
    [renderEncoder endEncoding];

    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];

    CVPixelBufferRelease(pixelBufferCopy);
}

#pragma mark - Metal

- (void)setupMetal {
    // 创建两个不同的pipeline state
    id<MTLLibrary> library = [_device newDefaultLibrary];
    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"vertexShader"];
    id<MTLFunction> yuvFragmentFunction = [library newFunctionWithName:@"yuvToRGBFragmentShader"];
    id<MTLFunction> nv12FragmentFunction = [library newFunctionWithName:@"nv12ToRGBFragmentShader"];
    
    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = yuvFragmentFunction; // 默认使用YUV
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    
    NSError *error;
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    
    // 创建NV12 pipeline
    pipelineDescriptor.fragmentFunction = nv12FragmentFunction;
    _nv12PipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
}
@end
