#import "IOSVideoRenderer.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <AVFoundation/AVFoundation.h>

#import "VideoFrame.h"

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
    AVSampleBufferDisplayLayer *_sampleBufferDisplayLayer;
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

    _mtkView.hidden = YES;
    _commandQueue = [_device newCommandQueue];

    _sampleBufferDisplayLayer = [[AVSampleBufferDisplayLayer alloc] init];
    CMTimebaseRef controlTimebase = NULL;
        CMTimebaseCreateWithMasterClock(kCFAllocatorDefault, CMClockGetHostTimeClock(), &controlTimebase);
    _sampleBufferDisplayLayer.controlTimebase = controlTimebase;
        CMTimebaseSetTime(controlTimebase, kCMTimeZero); // 设置初始时间
        CMTimebaseSetRate(controlTimebase, 1.0); // 设置播放速率
    _sampleBufferDisplayLayer.frame = _containerView.bounds;
    [_containerView.layer addSublayer:_sampleBufferDisplayLayer];

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
    if (frame.mFormat == yffplayer::PixelFormat::VIDEOTOOLBOX) {
        dispatch_sync(_renderQueue, ^{
            CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)frame.mData[3];
            if (!pixelBuffer) {
                return;
            }
            CMVideoFormatDescriptionRef videoInfo = NULL;
            OSStatus status = CMVideoFormatDescriptionCreateForImageBuffer(
                                                                           NULL,
                                                                           pixelBuffer,
                                                                           &videoInfo
                                                                           );
            if (status != noErr) {
                // 错误处理
            }

            // 3. 创建 CMSampleTimingInfo
            CMSampleTimingInfo timing = {
                .duration = CMTimeMakeWithSeconds(frame.mDuration / 1000.0, 1000),
                .presentationTimeStamp = CMTimeMakeWithSeconds(frame.mPts / 1000.0, 1000),
                .decodeTimeStamp = kCMTimeInvalid
            };

            // 4. 创建 CMSampleBuffer
            CMSampleBufferRef sampleBuffer = NULL;
            status = CMSampleBufferCreateForImageBuffer(
                                                        kCFAllocatorDefault,
                                                        pixelBuffer,
                                                        true,          // dataReady
                                                        NULL,          // makeDataReadyCallback
                                                        NULL,          // refcon
                                                        videoInfo,
                                                        &timing,
                                                        &sampleBuffer
                                                        );
            CFRelease(videoInfo);

            if (status != noErr) {
                // 错误处理
            }

            [_sampleBufferDisplayLayer enqueueSampleBuffer:sampleBuffer];

            CFRelease(sampleBuffer);

        });
    } else if (frame.mFormat == yffplayer::PixelFormat::YUV420P) {
        dispatch_sync(_renderQueue, ^{
            [self createTexturesForFrame:frame];
            self->_viewportSize = (vector_uint2){(uint32_t)frame.mWidth, (uint32_t)frame.mHeight};
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->_mtkView setNeedsDisplay];
            });
        });
    }
}

- (void)createTexturesForFrame:(const yffplayer::VideoFrame &)frame {
    if (frame.mWidth % 2 != 0 || frame.mHeight % 2 != 0) {
        NSLog(@"Invalid frame size: width=%d, height=%d", frame.mWidth, frame.mHeight);
        return;
    }

    MTLTextureDescriptor *textureDescriptor = [[MTLTextureDescriptor alloc] init];

    textureDescriptor.pixelFormat = MTLPixelFormatR8Unorm;
    textureDescriptor.width = frame.mWidth;
    textureDescriptor.height = frame.mHeight;
    textureDescriptor.usage = MTLTextureUsageShaderRead;

    _yTexture = [_device newTextureWithDescriptor:textureDescriptor];
    [_yTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth, frame.mHeight)
                 mipmapLevel:0
                   withBytes:frame.mData[0]
                 bytesPerRow:frame.mLinesize[0]];

    textureDescriptor.width = frame.mWidth / 2;
    textureDescriptor.height = frame.mHeight / 2;

    _uTexture = [_device newTextureWithDescriptor:textureDescriptor];
    [_uTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                 mipmapLevel:0
                   withBytes:frame.mData[1]
                 bytesPerRow:frame.mLinesize[1]];

    _vTexture = [_device newTextureWithDescriptor:textureDescriptor];
    [_vTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                 mipmapLevel:0
                   withBytes:frame.mData[2]
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
