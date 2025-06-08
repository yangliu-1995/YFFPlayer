#import "MetalVideoRenderer.h"
#import "VideoFrame.h"
#import <Metal/Metal.h>
#import <simd/simd.h>

// Vertex structure for quad rendering
typedef struct {
    vector_float2 position;
    vector_float2 texCoord;
} Vertex;

// Uniform structure for color adjustments
typedef struct {
    float brightness;
    float contrast;
    float saturation;
} ColorUniforms;

@interface MetalVideoRenderer ()

@property(nonatomic, readonly) CAMetalLayer *metalLayer;
@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLRenderPipelineState> yuv420pPipelineState;
@property (nonatomic, strong) id<MTLRenderPipelineState> nv12PipelineState;
@property (nonatomic, strong) id<MTLRenderPipelineState> rgb24PipelineState;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, strong) id<MTLBuffer> uniformBuffer;

// Textures for different formats
@property (nonatomic, strong) id<MTLTexture> yTexture;   // Y plane for YUV formats
@property (nonatomic, strong) id<MTLTexture> uTexture;   // U plane for YUV420P format
@property (nonatomic, strong) id<MTLTexture> vTexture;   // V plane for YUV420P format
@property (nonatomic, strong) id<MTLTexture> uvTexture;  // UV plane for NV12 format
@property (nonatomic, strong) id<MTLTexture> rgbTexture; // RGB texture for RGB24 format

@property (nonatomic, assign) yffplayer::PixelFormat currentFormat;
@property (nonatomic, assign) CGSize videoSize;

@property (nonatomic) dispatch_queue_t renderQueue;
@property (nonatomic) CVPixelBufferRef currentPixelBuffer;
@property (nonatomic) CVMetalTextureCacheRef textureCache;

@end

@implementation MetalVideoRenderer

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithView:(PlatformView *)view {
    self = [super initWithFrame:view.frame];
    if (self) {
        // Initialize default values
        _brightness = 0.0f;
        _contrast = 1.0f;
        _saturation = 1.0f;
        _currentFormat = yffplayer::PixelFormat::YUV420P;
        _videoSize = CGSizeZero;
        _renderQueue = dispatch_queue_create("com.yffplayer.render.video", DISPATCH_QUEUE_SERIAL);
        
        [view addSubview:self];
        self.translatesAutoresizingMaskIntoConstraints = NO;
        [NSLayoutConstraint activateConstraints:@[
            [self.topAnchor constraintEqualToAnchor:view.topAnchor],
            [self.leadingAnchor constraintEqualToAnchor:view.leadingAnchor],
            [self.trailingAnchor constraintEqualToAnchor:view.trailingAnchor],
            [self.bottomAnchor constraintEqualToAnchor:view.bottomAnchor]
        ]];
        
        // Setup Metal
        [self setupMetal];
        
        // Setup render pipeline
        [self setupRenderPipeline];
        
        // Setup vertex buffer
        [self setupVertexBuffer];
        
        // Setup uniform buffer
        [self setupUniformBuffer];
    }
    return self;
}

- (CAMetalLayer *)metalLayer {
    return static_cast<CAMetalLayer*>(self.layer);
}

- (void)setupMetal {
    self.device = MTLCreateSystemDefaultDevice();
    if (!self.device) {
        NSLog(@"Metal is not supported on this device");
        return;
    }
    
    self.metalLayer.device = self.device;
    self.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    self.metalLayer.framebufferOnly = NO;
    
    self.commandQueue = [self.device newCommandQueue];

    CVMetalTextureCacheRef textureCache = NULL;
    CVReturn result = CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, self.device, NULL, &textureCache);
    if (result == kCVReturnSuccess) {
        self.textureCache = textureCache;
    } else {
        NSLog(@"Failed to create texture cache: %d", result);
    }
}

- (void)setupRenderPipeline {
    NSError *error = nil;
    NSBundle *frameworkBundle = [NSBundle bundleForClass:[self class]];
    NSURL *libraryURL = [frameworkBundle URLForResource:@"default" withExtension:@"metallib"];
    id<MTLLibrary> library = [self.device newLibraryWithURL:libraryURL error:&error];
    if (error) {
        NSLog(@"Error loading Metal library: %@", error.localizedDescription);
        return;
    }
    
    // Get shader functions
    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"vertex_main"];
    id<MTLFunction> fragmentYUV420PFunction = [library newFunctionWithName:@"fragment_yuv420p"];
    id<MTLFunction> fragmentNV12Function = [library newFunctionWithName:@"fragment_nv12"];
    id<MTLFunction> fragmentRGB24Function = [library newFunctionWithName:@"fragment_rgb24"];
    
    if (!vertexFunction || !fragmentYUV420PFunction || !fragmentNV12Function || !fragmentRGB24Function) {
        NSLog(@"Error: Could not load shader functions");
        return;
    }
    
    // Create vertex descriptor
    MTLVertexDescriptor *vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    // Position attribute
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    // Texture coordinate attribute
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = 8; // 2 floats * 4 bytes
    vertexDescriptor.attributes[1].bufferIndex = 0;
    // Layout
    vertexDescriptor.layouts[0].stride = 16; // 4 floats * 4 bytes
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    
    // Create YUV420P pipeline
    MTLRenderPipelineDescriptor *yuv420pDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    yuv420pDescriptor.vertexFunction = vertexFunction;
    yuv420pDescriptor.fragmentFunction = fragmentYUV420PFunction;
    yuv420pDescriptor.vertexDescriptor = vertexDescriptor;
    yuv420pDescriptor.colorAttachments[0].pixelFormat = self.metalLayer.pixelFormat;
    
    self.yuv420pPipelineState = [self.device newRenderPipelineStateWithDescriptor:yuv420pDescriptor error:&error];
    if (error) {
        NSLog(@"Error creating YUV420P pipeline state: %@", error.localizedDescription);
    }
    
    // Create NV12 pipeline
    MTLRenderPipelineDescriptor *nv12Descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    nv12Descriptor.vertexFunction = vertexFunction;
    nv12Descriptor.fragmentFunction = fragmentNV12Function;
    nv12Descriptor.vertexDescriptor = vertexDescriptor;
    nv12Descriptor.colorAttachments[0].pixelFormat = self.metalLayer.pixelFormat;
    
    self.nv12PipelineState = [self.device newRenderPipelineStateWithDescriptor:nv12Descriptor error:&error];
    if (error) {
        NSLog(@"Error creating NV12 pipeline state: %@", error.localizedDescription);
    }
    
    // Create RGB24 pipeline
    MTLRenderPipelineDescriptor *rgb24Descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    rgb24Descriptor.vertexFunction = vertexFunction;
    rgb24Descriptor.fragmentFunction = fragmentRGB24Function;
    rgb24Descriptor.vertexDescriptor = vertexDescriptor;
    rgb24Descriptor.colorAttachments[0].pixelFormat = self.metalLayer.pixelFormat;
    
    self.rgb24PipelineState = [self.device newRenderPipelineStateWithDescriptor:rgb24Descriptor error:&error];
    if (error) {
        NSLog(@"Error creating RGB24 pipeline state: %@", error.localizedDescription);
    }
}

- (void)setupVertexBuffer {
    // Create quad vertices
    Vertex vertices[] = {
        {{-1.0f, -1.0f}, {0.0f, 1.0f}}, // Bottom left
        {{ 1.0f, -1.0f}, {1.0f, 1.0f}}, // Bottom right
        {{-1.0f,  1.0f}, {0.0f, 0.0f}}, // Top left
        {{ 1.0f,  1.0f}, {1.0f, 0.0f}}  // Top right
    };
    
    self.vertexBuffer = [self.device newBufferWithBytes:vertices
                                                 length:sizeof(vertices)
                                                options:MTLResourceStorageModeShared];
}

- (void)setupUniformBuffer {
    self.uniformBuffer = [self.device newBufferWithLength:sizeof(ColorUniforms)
                                                  options:MTLResourceStorageModeShared];
}

- (void)setFps:(NSInteger)fps {
    _fps = MIN(MAX(0, fps), 60);
    // Note: fps is ignored as requested, using manual refresh
}

- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame {
    dispatch_sync(_renderQueue, ^{
        if (!frame.isValid()) {
            return;
        }

        self.currentFormat = frame.format_;
        self.videoSize = CGSizeMake(frame.width_, frame.height_);

        BOOL shouldDisplay = YES;

        switch (frame.format_) {
            case yffplayer::PixelFormat::YUV420P:
                [self createYUV420PTextures:frame];
                break;
            case yffplayer::PixelFormat::NV12:
                [self createNV12Textures:frame];
                break;
            case yffplayer::PixelFormat::RGB24:
                [self createRGB24Texture:frame];
                break;
            case yffplayer::PixelFormat::VIDEOTOOLBOX:
                shouldDisplay = [self createVTNV12Textures:frame];
                break;
        }

        if (!shouldDisplay) {
            NSLog(@"Failed to create textures for format: %d", (int)frame.format_);
            return;
        }

        // Update uniform buffer
        [self updateUniforms];
        
        // Manually trigger rendering
        NSTimeInterval s = CFAbsoluteTimeGetCurrent();
        [self drawFrame];
        NSTimeInterval c = CFAbsoluteTimeGetCurrent() - s;
        NSLog(@"draw cost: %f", c);
    });
}

- (void)createYUV420PTextures:(const yffplayer::VideoFrame&)frame {
    // Y plane texture
    MTLTextureDescriptor *yDescriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                         width:frame.width_
                                        height:frame.height_
                                     mipmapped:NO];
    yDescriptor.usage = MTLTextureUsageShaderRead;
    
    // U plane texture
    MTLTextureDescriptor *uDescriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                         width:frame.width_ / 2
                                        height:frame.height_ / 2
                                     mipmapped:NO];
    uDescriptor.usage = MTLTextureUsageShaderRead;
    
    // V plane texture
    MTLTextureDescriptor *vDescriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                         width:frame.width_ / 2
                                        height:frame.height_ / 2
                                     mipmapped:NO];
    vDescriptor.usage = MTLTextureUsageShaderRead;
    
    self.yTexture = [self.device newTextureWithDescriptor:yDescriptor];
    self.uTexture = [self.device newTextureWithDescriptor:uDescriptor];
    self.vTexture = [self.device newTextureWithDescriptor:vDescriptor];
    
    // Upload Y plane
    [self.yTexture replaceRegion:MTLRegionMake2D(0, 0, frame.width_, frame.height_)
                     mipmapLevel:0
                       withBytes:frame.data_[0]
                     bytesPerRow:frame.linesize_[0]];
    
    // Upload U plane
    [self.uTexture replaceRegion:MTLRegionMake2D(0, 0, frame.width_ / 2, frame.height_ / 2)
                     mipmapLevel:0
                       withBytes:frame.data_[1]
                     bytesPerRow:frame.linesize_[1]];
    
    // Upload V plane
    [self.vTexture replaceRegion:MTLRegionMake2D(0, 0, frame.width_ / 2, frame.height_ / 2)
                     mipmapLevel:0
                       withBytes:frame.data_[2]
                     bytesPerRow:frame.linesize_[2]];
}

- (void)createNV12Textures:(const yffplayer::VideoFrame &)frame {
    // Create Y texture
    MTLTextureDescriptor *yDescriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                         width:frame.width_
                                        height:frame.height_
                                     mipmapped:NO];
    yDescriptor.usage = MTLTextureUsageShaderRead;
    self.yTexture = [self.device newTextureWithDescriptor:yDescriptor];
    
    [self.yTexture replaceRegion:MTLRegionMake2D(0, 0, frame.width_, frame.height_)
                     mipmapLevel:0
                       withBytes:frame.data_[0]
                     bytesPerRow:frame.linesize_[0]];
    
    // Create UV texture (already interleaved in NV12)
    MTLTextureDescriptor *uvDescriptor = [MTLTextureDescriptor
             texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
                                          width:frame.width_ / 2
                                         height:frame.height_ / 2
                                      mipmapped:NO];
    uvDescriptor.usage = MTLTextureUsageShaderRead;
    self.uvTexture = [self.device newTextureWithDescriptor:uvDescriptor];
    
    [self.uvTexture replaceRegion:MTLRegionMake2D(0, 0, frame.width_ / 2, frame.height_ / 2)
                      mipmapLevel:0
                        withBytes:frame.data_[1]
                      bytesPerRow:frame.linesize_[1]];
}

- (void)createRGB24Texture:(const yffplayer::VideoFrame &)frame {
    // Convert RGB24 to RGBA for Metal
    NSUInteger rgbaDataSize = frame.width_ * frame.height_ * 4;
    uint8_t *rgbaData = (uint8_t *)malloc(rgbaDataSize);
    
    for (NSUInteger y = 0; y < frame.height_; y++) {
        for (NSUInteger x = 0; x < frame.width_; x++) {
            NSUInteger srcIndex = y * frame.linesize_[0] + x * 3;
            NSUInteger dstIndex = (y * frame.width_ + x) * 4;
            
            rgbaData[dstIndex] = frame.data_[0][srcIndex];     // R
            rgbaData[dstIndex + 1] = frame.data_[0][srcIndex + 1]; // G
            rgbaData[dstIndex + 2] = frame.data_[0][srcIndex + 2]; // B
            rgbaData[dstIndex + 3] = 255; // A
        }
    }
    
    MTLTextureDescriptor *rgbDescriptor = [MTLTextureDescriptor
              texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                           width:frame.width_
                                          height:frame.height_
                                       mipmapped:NO];
    rgbDescriptor.usage = MTLTextureUsageShaderRead;
    self.rgbTexture = [self.device newTextureWithDescriptor:rgbDescriptor];
    
    [self.rgbTexture replaceRegion:MTLRegionMake2D(0, 0, frame.width_, frame.height_)
                       mipmapLevel:0
                         withBytes:rgbaData
                       bytesPerRow:frame.width_ * 4];
    
    free(rgbaData);
}

- (BOOL)createVTNV12Textures:(const yffplayer::VideoFrame&)frame {
    CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)frame.data_[3];
    if (!pixelBuffer) {
        NSLog(@"Error: CVPixelBufferRef is null");
        return NO;
    }

    if (self.currentPixelBuffer) {
        CVPixelBufferRelease(self.currentPixelBuffer);
    }
    self.currentPixelBuffer = CVPixelBufferRetain(pixelBuffer);

    CVMetalTextureCacheFlush(self.textureCache, 0);

    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    CVMetalTextureRef yTextureRef = NULL;
    CVReturn result = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        self.textureCache,
        pixelBuffer,
        NULL,
        MTLPixelFormatR8Unorm,
        width,
        height,
        0,
        &yTextureRef
    );

    if (result != kCVReturnSuccess || !yTextureRef) {
        NSLog(@"Error creating Y texture: %d", result);
        return NO;
    }

    CVMetalTextureRef uvTextureRef = NULL;
    result = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        self.textureCache,
        pixelBuffer,
        NULL,
        MTLPixelFormatRG8Unorm,
        width / 2,
        height / 2,
        1,
        &uvTextureRef
    );

    if (result != kCVReturnSuccess || !uvTextureRef) {
        NSLog(@"Error creating UV texture: %d", result);
        CFRelease(yTextureRef);
        return NO;
    }

    self.yTexture = CVMetalTextureGetTexture(yTextureRef);
    self.uvTexture = CVMetalTextureGetTexture(uvTextureRef);

    CFRelease(yTextureRef);
    CFRelease(uvTextureRef);

    return YES;
}

- (void)drawFrame {
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [self.metalLayer nextDrawable];
        if (!drawable) {
            return;
        }
        
        id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
        if (!commandBuffer) {
            return;
        }
        
        MTLRenderPassDescriptor *renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        renderPassDescriptor.colorAttachments[0].texture = drawable.texture;
        renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        
        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        if (!renderEncoder) {
            return;
        }

        // Select appropriate pipeline and set textures based on pixel format
        switch (self.currentFormat) {
            case yffplayer::PixelFormat::YUV420P:
                if (self.yuv420pPipelineState && self.yTexture && self.uTexture && self.vTexture) {
                    [renderEncoder setRenderPipelineState:self.yuv420pPipelineState];
                    [renderEncoder setFragmentTexture:self.yTexture atIndex:0];
                    [renderEncoder setFragmentTexture:self.uTexture atIndex:1];
                    [renderEncoder setFragmentTexture:self.vTexture atIndex:2];
                } else {
                    [renderEncoder endEncoding];
                    return;
                }
                break;
            case yffplayer::PixelFormat::NV12:
                if (self.nv12PipelineState && self.yTexture && self.uvTexture) {
                    [renderEncoder setRenderPipelineState:self.nv12PipelineState];
                    [renderEncoder setFragmentTexture:self.yTexture atIndex:0];
                    [renderEncoder setFragmentTexture:self.uvTexture atIndex:1];
                } else {
                    [renderEncoder endEncoding];
                    return;
                }
                break;
            case yffplayer::PixelFormat::RGB24:
                if (self.rgb24PipelineState && self.rgbTexture) {
                    [renderEncoder setRenderPipelineState:self.rgb24PipelineState];
                    [renderEncoder setFragmentTexture:self.rgbTexture atIndex:0];
                } else {
                    [renderEncoder endEncoding];
                    return;
                }
                break;
            case yffplayer::PixelFormat::VIDEOTOOLBOX:
                if (self.nv12PipelineState && self.yTexture && self.uvTexture) {
                    [renderEncoder setRenderPipelineState:self.nv12PipelineState];
                    [renderEncoder setFragmentTexture:self.yTexture atIndex:0];
                    [renderEncoder setFragmentTexture:self.uvTexture atIndex:1];
                } else {
                    [renderEncoder endEncoding];
                    return;
                }
                break;
            default:
                NSLog(@"Unsupported pixel format: %d", (int)self.currentFormat);
                [renderEncoder endEncoding];
                return;
        }

        [renderEncoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
        [renderEncoder setFragmentBuffer:self.uniformBuffer offset:0 atIndex:0];

        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [renderEncoder endEncoding];

        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    }
}

- (void)updateUniforms {
    ColorUniforms *uniforms = (ColorUniforms *)[self.uniformBuffer contents];
    uniforms->brightness = self.brightness;
    uniforms->contrast = self.contrast;
    uniforms->saturation = self.saturation;
}

- (void)dealloc {
    if (self.currentPixelBuffer) {
        CVPixelBufferRelease(self.currentPixelBuffer);
        self.currentPixelBuffer = NULL;
    }
    
    if (self.textureCache) {
        CFRelease(self.textureCache);
        self.textureCache = NULL;
    }
}

#pragma mark - Property Setters

- (void)setBrightness:(float)brightness {
    _brightness = clamp(brightness, -1.0f, 1.0f);
}

- (void)setContrast:(float)contrast {
    _contrast = fmax(contrast, 0.0f);
}

- (void)setSaturation:(float)saturation {
    _saturation = fmax(saturation, 0.0f);
}

static inline float clamp(float value, float min, float max) {
    return fmax(min, fmin(max, value));
}

@end
