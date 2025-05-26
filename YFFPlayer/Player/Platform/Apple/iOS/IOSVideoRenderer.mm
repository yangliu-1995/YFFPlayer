#import "IOSVideoRenderer.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

@interface IOSVideoRenderer () <MTKViewDelegate>
{
    MTKView *_mtkView;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLTexture> _yTexture;
    id<MTLTexture> _uTexture;
    id<MTLTexture> _vTexture;
    vector_uint2 _viewportSize;
}

@property (nonatomic, strong) UIView *containerView;

@end

@implementation IOSVideoRenderer

- (instancetype)initWithView:(UIView *)view {
    self = [super init];
    if (self) {
        _containerView = view;
        [self setupMetal];
    }
    return self;
}

- (void)setupMetal {
    _device = MTLCreateSystemDefaultDevice();
    _mtkView = [[MTKView alloc] initWithFrame:_containerView.bounds device:_device];
    _mtkView.delegate = self;
    _mtkView.preferredFramesPerSecond = 60;
    [_containerView addSubview:_mtkView];

    // 设置 MTKView 自动调整大小
    _mtkView.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [_mtkView.leadingAnchor constraintEqualToAnchor:_containerView.leadingAnchor],
        [_mtkView.trailingAnchor constraintEqualToAnchor:_containerView.trailingAnchor],
        [_mtkView.topAnchor constraintEqualToAnchor:_containerView.topAnchor],
        [_mtkView.bottomAnchor constraintEqualToAnchor:_containerView.bottomAnchor]
    ]];

    _commandQueue = [_device newCommandQueue];

    [self setupPipeline];
}

- (void)setupPipeline {
    id<MTLLibrary> defaultLibrary = [_device newDefaultLibrary];

    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"YUVToRGBPipeline";

    // Vertex and fragment shaders
    id<MTLFunction> vertexFunction = [defaultLibrary newFunctionWithName:@"vertexShader"];
    id<MTLFunction> fragmentFunction = [defaultLibrary newFunctionWithName:@"yuvToRGBFragmentShader"];

    pipelineDescriptor.vertexFunction = vertexFunction;
    pipelineDescriptor.fragmentFunction = fragmentFunction;

    pipelineDescriptor.colorAttachments[0].pixelFormat = _mtkView.colorPixelFormat;

    NSError *error = nil;
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!_pipelineState) {
        NSLog(@"Failed to create pipeline state: %@", error);
    }
}

- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame {
    if (frame.mFormat != yffplayer::PixelFormat::YUV420P) {
        NSLog(@"Only YUV420P format is supported");
        return;
    }

    // 创建 YUV 纹理
    [self createTexturesForFrame:frame];

    // 更新视口大小
    _viewportSize = (vector_uint2){(uint32_t)frame.mWidth, (uint32_t)frame.mHeight};

    dispatch_async(dispatch_get_main_queue(), ^{
        [_mtkView setNeedsDisplay];
    });
}

- (void)createTexturesForFrame:(const yffplayer::VideoFrame &)frame {
    MTLTextureDescriptor *textureDescriptor = [[MTLTextureDescriptor alloc] init];

    // Y 通道
    textureDescriptor.pixelFormat = MTLPixelFormatR8Unorm;
    textureDescriptor.width = frame.mWidth;
    textureDescriptor.height = frame.mHeight;

    _yTexture = [_device newTextureWithDescriptor:textureDescriptor];

    // U 和 V 通道（YUV420P 的 UV 分量是 1/4 大小）
    textureDescriptor.width = frame.mWidth / 2;
    textureDescriptor.height = frame.mHeight / 2;

    _uTexture = [_device newTextureWithDescriptor:textureDescriptor];
    _vTexture = [_device newTextureWithDescriptor:textureDescriptor];

    // 上传 YUV 数据
    const uint8_t *data = frame.mData.data();
    size_t ySize = frame.mWidth * frame.mHeight;
    size_t uvSize = ySize / 4;

    [_yTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth, frame.mHeight)
                mipmapLevel:0
                  withBytes:data
                bytesPerRow:frame.mWidth];

    [_uTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                mipmapLevel:0
                  withBytes:data + ySize
                bytesPerRow:frame.mWidth / 2];

    [_vTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                mipmapLevel:0
                  withBytes:data + ySize + uvSize
                bytesPerRow:frame.mWidth / 2];
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    _viewportSize = (vector_uint2){(uint32_t)size.width, (uint32_t)size.height};
}

- (void)drawInMTKView:(MTKView *)view {
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;

    if (renderPassDescriptor != nil && _yTexture != nil) {
        id<MTLRenderCommandEncoder> renderEncoder =
            [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

        [renderEncoder setRenderPipelineState:_pipelineState];

        [renderEncoder setVertexBytes:&_viewportSize
                              length:sizeof(_viewportSize)
                             atIndex:0];

        [renderEncoder setFragmentTexture:_yTexture atIndex:0];
        [renderEncoder setFragmentTexture:_uTexture atIndex:1];
        [renderEncoder setFragmentTexture:_vTexture atIndex:2];

        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                         vertexStart:0
                         vertexCount:4];

        [renderEncoder endEncoding];

        [commandBuffer presentDrawable:view.currentDrawable];
    }

    [commandBuffer commit];
}

@end
