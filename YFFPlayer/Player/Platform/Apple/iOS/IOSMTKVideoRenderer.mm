#import "IOSMTKVideoRenderer.h"
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

@interface IOSMTKVideoRenderer ()

@property (nonatomic, strong) MTKView *mtkView;
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

@end

@implementation IOSMTKVideoRenderer

- (instancetype)initWithView:(UIView *)view {
    self = [super init];
    if (self) {
        // Initialize default values
        _brightness = 0.0f;
        _contrast = 1.0f;
        _saturation = 1.0f;
        _currentFormat = yffplayer::PixelFormat::YUV420P;
        _videoSize = CGSizeZero;
        _renderQueue = dispatch_queue_create("com.example.iosvideorenderer.renderqueue", DISPATCH_QUEUE_SERIAL);
        // Setup MTKView
        [self setupMTKViewWithParentView:view];
        
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

- (void)setupMTKViewWithParentView:(UIView *)parentView {
    self.device = MTLCreateSystemDefaultDevice();
    if (!self.device) {
        NSLog(@"Metal is not supported on this device");
        return;
    }
    
    self.mtkView = [[MTKView alloc] initWithFrame:parentView.bounds device:self.device];
    self.mtkView.delegate = self;
    self.mtkView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.mtkView.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    self.mtkView.framebufferOnly = NO;
    
    [parentView addSubview:self.mtkView];
    
    // Setup auto layout
    self.mtkView.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.mtkView.topAnchor constraintEqualToAnchor:parentView.topAnchor],
        [self.mtkView.leadingAnchor constraintEqualToAnchor:parentView.leadingAnchor],
        [self.mtkView.trailingAnchor constraintEqualToAnchor:parentView.trailingAnchor],
        [self.mtkView.bottomAnchor constraintEqualToAnchor:parentView.bottomAnchor]
    ]];
}

- (void)setupMetal {
    self.commandQueue = [self.device newCommandQueue];
}

- (void)setupRenderPipeline {
    NSError *error = nil;
    
    // Load shader library from bundle
    id<MTLLibrary> library = [self.device newDefaultLibrary];
    if (!library) {
        NSLog(@"Error: Could not load default Metal library");
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
    yuv420pDescriptor.colorAttachments[0].pixelFormat = self.mtkView.colorPixelFormat;
    
    self.yuv420pPipelineState = [self.device newRenderPipelineStateWithDescriptor:yuv420pDescriptor error:&error];
    if (error) {
        NSLog(@"Error creating YUV420P pipeline state: %@", error.localizedDescription);
    }
    
    // Create NV12 pipeline
    MTLRenderPipelineDescriptor *nv12Descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    nv12Descriptor.vertexFunction = vertexFunction;
    nv12Descriptor.fragmentFunction = fragmentNV12Function;
    nv12Descriptor.vertexDescriptor = vertexDescriptor;
    nv12Descriptor.colorAttachments[0].pixelFormat = self.mtkView.colorPixelFormat;
    
    self.nv12PipelineState = [self.device newRenderPipelineStateWithDescriptor:nv12Descriptor error:&error];
    if (error) {
        NSLog(@"Error creating NV12 pipeline state: %@", error.localizedDescription);
    }
    
    // Create RGB24 pipeline
    MTLRenderPipelineDescriptor *rgb24Descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    rgb24Descriptor.vertexFunction = vertexFunction;
    rgb24Descriptor.fragmentFunction = fragmentRGB24Function;
    rgb24Descriptor.vertexDescriptor = vertexDescriptor;
    rgb24Descriptor.colorAttachments[0].pixelFormat = self.mtkView.colorPixelFormat;
    
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

- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame {
    dispatch_sync(_renderQueue, ^{
        if (!frame.isValid()) {
            return;
        }

        self.currentFormat = frame.mFormat;
        self.videoSize = CGSizeMake(frame.mWidth, frame.mHeight);

        switch (frame.mFormat) {
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
                [self createVTNV12Textures:frame];
                break;
        }

        // Update uniform buffer
        [self updateUniforms];

        dispatch_sync(dispatch_get_main_queue(), ^{
            [self.mtkView setNeedsDisplay];
        });
    });
}

- (void)createYUV420PTextures:(const yffplayer::VideoFrame&)frame {
    // Y plane texture
    MTLTextureDescriptor *yDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                           width:frame.mWidth
                                                                                          height:frame.mHeight
                                                                                       mipmapped:NO];
    yDescriptor.usage = MTLTextureUsageShaderRead;
    
    // U plane texture
    MTLTextureDescriptor *uDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                            width:frame.mWidth / 2
                                                                                           height:frame.mHeight / 2
                                                                                        mipmapped:NO];
    uDescriptor.usage = MTLTextureUsageShaderRead;
    
    // V plane texture
    MTLTextureDescriptor *vDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                            width:frame.mWidth / 2
                                                                                           height:frame.mHeight / 2
                                                                                        mipmapped:NO];
    vDescriptor.usage = MTLTextureUsageShaderRead;
    
    self.yTexture = [self.device newTextureWithDescriptor:yDescriptor];
    self.uTexture = [self.device newTextureWithDescriptor:uDescriptor];
    self.vTexture = [self.device newTextureWithDescriptor:vDescriptor];
    
    // Upload Y plane
    [self.yTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth, frame.mHeight)
                     mipmapLevel:0
                       withBytes:frame.mData[0]
                     bytesPerRow:frame.mLinesize[0]];
    
    // Upload U plane
    [self.uTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                     mipmapLevel:0
                       withBytes:frame.mData[1]
                     bytesPerRow:frame.mLinesize[1]];
    
    // Upload V plane
    [self.vTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                     mipmapLevel:0
                       withBytes:frame.mData[2]
                     bytesPerRow:frame.mLinesize[2]];
}

- (void)createNV12Textures:(const yffplayer::VideoFrame &)frame {
    // Create Y texture
    MTLTextureDescriptor *yDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                                           width:frame.mWidth
                                                                                          height:frame.mHeight
                                                                                       mipmapped:NO];
    yDescriptor.usage = MTLTextureUsageShaderRead;
    self.yTexture = [self.device newTextureWithDescriptor:yDescriptor];
    
    [self.yTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth, frame.mHeight)
                     mipmapLevel:0
                       withBytes:frame.mData[0]
                     bytesPerRow:frame.mLinesize[0]];
    
    // Create UV texture (already interleaved in NV12)
    MTLTextureDescriptor *uvDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm
                                                                                            width:frame.mWidth / 2
                                                                                           height:frame.mHeight / 2
                                                                                        mipmapped:NO];
    uvDescriptor.usage = MTLTextureUsageShaderRead;
    self.uvTexture = [self.device newTextureWithDescriptor:uvDescriptor];
    
    [self.uvTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth / 2, frame.mHeight / 2)
                      mipmapLevel:0
                        withBytes:frame.mData[1]
                      bytesPerRow:frame.mLinesize[1]];
}

- (void)createRGB24Texture:(const yffplayer::VideoFrame &)frame {
    // Convert RGB24 to RGBA for Metal
    NSUInteger rgbaDataSize = frame.mWidth * frame.mHeight * 4;
    uint8_t *rgbaData = (uint8_t *)malloc(rgbaDataSize);
    
    for (NSUInteger y = 0; y < frame.mHeight; y++) {
        for (NSUInteger x = 0; x < frame.mWidth; x++) {
            NSUInteger srcIndex = y * frame.mLinesize[0] + x * 3;
            NSUInteger dstIndex = (y * frame.mWidth + x) * 4;
            
            rgbaData[dstIndex] = frame.mData[0][srcIndex];     // R
            rgbaData[dstIndex + 1] = frame.mData[0][srcIndex + 1]; // G
            rgbaData[dstIndex + 2] = frame.mData[0][srcIndex + 2]; // B
            rgbaData[dstIndex + 3] = 255; // A
        }
    }
    
    MTLTextureDescriptor *rgbDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                             width:frame.mWidth
                                                                                            height:frame.mHeight
                                                                                         mipmapped:NO];
    rgbDescriptor.usage = MTLTextureUsageShaderRead;
    self.rgbTexture = [self.device newTextureWithDescriptor:rgbDescriptor];
    
    [self.rgbTexture replaceRegion:MTLRegionMake2D(0, 0, frame.mWidth, frame.mHeight)
                       mipmapLevel:0
                         withBytes:rgbaData
                       bytesPerRow:frame.mWidth * 4];
    
    free(rgbaData);
}

- (void)createVTNV12Textures:(const yffplayer::VideoFrame&)frame {
    // Get CVPixelBufferRef from VideoToolbox hardware decoder
    CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)frame.mData[3];
    if (!pixelBuffer) {
        NSLog(@"Error: CVPixelBufferRef is null");
        return;
    }
    
    // Create texture cache if not exists
    static CVMetalTextureCacheRef textureCache = NULL;
    if (!textureCache) {
        CVReturn result = CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, self.device, NULL, &textureCache);
        if (result != kCVReturnSuccess) {
            NSLog(@"Error creating CVMetalTextureCache: %d", result);
            return;
        }
    }
    
    // Get pixel buffer dimensions
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    
    // Create Y texture (luminance plane)
    CVMetalTextureRef yTextureRef = NULL;
    CVReturn result = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        textureCache,
        pixelBuffer,
        NULL,
        MTLPixelFormatR8Unorm,
        width,
        height,
        0, // plane index for Y
        &yTextureRef
    );
    
    if (result != kCVReturnSuccess || !yTextureRef) {
        NSLog(@"Error creating Y texture from CVPixelBuffer: %d", result);
        return;
    }
    
    self.yTexture = CVMetalTextureGetTexture(yTextureRef);
    
    // Create UV texture (chroma plane)
    CVMetalTextureRef uvTextureRef = NULL;
    result = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        textureCache,
        pixelBuffer,
        NULL,
        MTLPixelFormatRG8Unorm,
        width / 2,
        height / 2,
        1, // plane index for UV
        &uvTextureRef
    );
    
    if (result != kCVReturnSuccess || !uvTextureRef) {
        NSLog(@"Error creating UV texture from CVPixelBuffer: %d", result);
        CFRelease(yTextureRef);
        return;
    }
    
    self.uvTexture = CVMetalTextureGetTexture(uvTextureRef);
    
    // Release texture references (textures are retained by Metal)
    CFRelease(yTextureRef);
    CFRelease(uvTextureRef);
}

- (void)updateUniforms {
    ColorUniforms *uniforms = (ColorUniforms *)[self.uniformBuffer contents];
    uniforms->brightness = self.brightness;
    uniforms->contrast = self.contrast;
    uniforms->saturation = self.saturation;
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // Handle view size changes
}

- (void)drawInMTKView:(MTKView *)view {
    dispatch_async(_renderQueue, ^{
        id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
        MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;

        if (renderPassDescriptor) {
            id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];

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
                        [commandBuffer commit];
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
                        [commandBuffer commit];
                        return;
                    }
                    break;
                case yffplayer::PixelFormat::RGB24:
                    if (self.rgb24PipelineState && self.rgbTexture) {
                        [renderEncoder setRenderPipelineState:self.rgb24PipelineState];
                        [renderEncoder setFragmentTexture:self.rgbTexture atIndex:0];
                    } else {
                        [renderEncoder endEncoding];
                        [commandBuffer commit];
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
                        [commandBuffer commit];
                        return;
                    }
                    break;
                default:
                    NSLog(@"Unsupported pixel format: %d", (int)self.currentFormat);
                    [renderEncoder endEncoding];
                    [commandBuffer commit];
                    return;
            }

            [renderEncoder setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
            [renderEncoder setFragmentBuffer:self.uniformBuffer offset:0 atIndex:0];

            [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [renderEncoder endEncoding];

            [commandBuffer presentDrawable:view.currentDrawable];
        }

        [commandBuffer commit];
    });
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
