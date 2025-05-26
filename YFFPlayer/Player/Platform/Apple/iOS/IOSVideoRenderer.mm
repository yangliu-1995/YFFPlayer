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
@property (nonatomic) dispatch_queue_t renderQueue;

@end

@implementation IOSVideoRenderer

- (instancetype)initWithView:(UIView *)view {
    self = [super init];
    if (self) {
        _containerView = view;
        _renderQueue = dispatch_queue_create("com.example.iosvideorenderer.renderqueue", DISPATCH_QUEUE_SERIAL);
        [self setupMetal];
    }
    return self;
}

- (void)setupMetal {
    _device = MTLCreateSystemDefaultDevice();
    _mtkView = [[MTKView alloc] initWithFrame:_containerView.bounds device:_device];
    _mtkView.delegate = self;
    _mtkView.preferredFramesPerSecond = 60;
    _mtkView.framebufferOnly = NO;
    [_containerView addSubview:_mtkView];

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

    dispatch_sync(_renderQueue, ^{
        [self createTexturesForFrame:frame];
        self->_viewportSize = (vector_uint2){(uint32_t)frame.mWidth, (uint32_t)frame.mHeight};
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_mtkView setNeedsDisplay];
        });
    });
}

- (void)createTexturesForFrame:(const yffplayer::VideoFrame &)frame {
    if (frame.mWidth % 2 != 0 || frame.mHeight % 2 != 0) {
        NSLog(@"Invalid frame size: width=%d, height=%d", frame.mWidth, frame.mHeight);
        return;
    }

    MTLTextureDescriptor *textureDescriptor = [[MTLTextureDescriptor alloc] init];
    const uint8_t *data = frame.mData.data();

    size_t ySize = frame.mWidth * frame.mHeight;
    size_t uSize = (frame.mWidth / 2) * (frame.mHeight / 2);

    textureDescriptor.pixelFormat = MTLPixelFormatR8Unorm;
    textureDescriptor.width = frame.mWidth;
    textureDescriptor.height = frame.mHeight;
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    _yTexture = [_device newTextureWithDescriptor:textureDescriptor];
    [_yTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth, frame.mHeight)
                 mipmapLevel:0
                   withBytes:data
                 bytesPerRow:frame.mLinesize[0]];

    textureDescriptor.width = frame.mWidth / 2;
    textureDescriptor.height = frame.mHeight / 2;

    _uTexture = [_device newTextureWithDescriptor:textureDescriptor];
    [_uTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                 mipmapLevel:0
                   withBytes:data + ySize
                 bytesPerRow:frame.mLinesize[1]];

    _vTexture = [_device newTextureWithDescriptor:textureDescriptor];
    [_vTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                 mipmapLevel:0
                   withBytes:data + ySize + uSize
                 bytesPerRow:frame.mLinesize[2]];
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    dispatch_async(_renderQueue, ^{
        self->_viewportSize = (vector_uint2){(uint32_t)size.width, (uint32_t)size.height};
    });
}

- (void)drawInMTKView:(MTKView *)view {
    dispatch_sync(_renderQueue, ^{
        id<MTLCommandBuffer> commandBuffer = [self->_commandQueue commandBuffer];
        MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;

        if (renderPassDescriptor && self->_yTexture && self->_uTexture && self->_vTexture) {
            id<MTLRenderCommandEncoder> renderEncoder =
                [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

            [renderEncoder setRenderPipelineState:self->_pipelineState];
            [renderEncoder setFragmentTexture:self->_yTexture atIndex:0];
            [renderEncoder setFragmentTexture:self->_uTexture atIndex:1];
            [renderEncoder setFragmentTexture:self->_vTexture atIndex:2];

            [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:6];
            [renderEncoder endEncoding];

            [commandBuffer presentDrawable:view.currentDrawable];
        }

        [commandBuffer commit];
    });
}

@end
