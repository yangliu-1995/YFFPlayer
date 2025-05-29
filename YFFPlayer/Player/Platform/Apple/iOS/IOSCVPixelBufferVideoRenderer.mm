#import "IOSCVPixelBufferVideoRenderer.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreImage/CoreImage.h>
#import <VideoToolbox/VideoToolbox.h>
#import "VideoFrame.h"

@interface IOSCVPixelBufferVideoRenderer ()
{
    AVSampleBufferDisplayLayer *_displayLayer;
    UIView *_containerView;
    dispatch_queue_t _renderQueue;
    
    // Core Image相关
    CIContext *_ciContext;
    CIFilter *_colorControlsFilter;
    
    // 颜色调节参数
    float _brightness;  // -1.0 到 1.0
    float _contrast;    // 0.0 到 2.0
    float _saturation;  // 0.0 到 2.0
}

@end

@implementation IOSCVPixelBufferVideoRenderer

- (instancetype)initWithView:(UIView *)view {
    self = [super init];
    if (self) {
        _containerView = view;
        _renderQueue = dispatch_queue_create("com.yffplayer.cvpixelbuffer.render", DISPATCH_QUEUE_SERIAL);
        
        // 设置默认颜色调节参数
        _brightness = 0.0f;
        _contrast = 1.0f;
        _saturation = 1.0f;
        
        [self setupDisplayLayer];
        [self setupCoreImage];
    }
    return self;
}

- (void)setupDisplayLayer {
    _displayLayer = [[AVSampleBufferDisplayLayer alloc] init];
    _displayLayer.frame = _containerView.bounds;
    _displayLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    
    // 创建控制时间基准
    CMTimebaseRef controlTimebase = NULL;
    CMTimebaseCreateWithMasterClock(kCFAllocatorDefault, CMClockGetHostTimeClock(), &controlTimebase);
    _displayLayer.controlTimebase = controlTimebase;
    CMTimebaseSetTime(controlTimebase, kCMTimeZero);
    CMTimebaseSetRate(controlTimebase, 1.0);
    
    [_containerView.layer addSublayer:_displayLayer];
    
    // 设置自动布局
    _displayLayer.frame = _containerView.bounds;
}

- (void)setupCoreImage {
    // 创建CIContext
    _ciContext = [CIContext context];
    
    // 创建颜色控制滤镜
    _colorControlsFilter = [CIFilter filterWithName:@"CIColorControls"];
}

- (void)renderVideoFrame:(const yffplayer::VideoFrame &)frame {
    dispatch_sync(_renderQueue, ^{
        CVPixelBufferRef pixelBuffer = [self createCVPixelBufferFromVideoFrame:frame];
        if (!pixelBuffer) {
            NSLog(@"Failed to create CVPixelBuffer from VideoFrame");
            return;
        }
        
        // 应用颜色调节
        CVPixelBufferRef processedPixelBuffer = [self applyColorAdjustmentToPixelBuffer:pixelBuffer];
        
        // 创建CMSampleBuffer
        CMSampleBufferRef sampleBuffer = [self createSampleBufferFromPixelBuffer:processedPixelBuffer
                                                                        withPTS:frame.mPts
                                                                       duration:frame.mDuration];
        
        if (sampleBuffer) {
            [self->_displayLayer enqueueSampleBuffer:sampleBuffer];
            CFRelease(sampleBuffer);
        }
        
        // 释放像素缓冲区
        if (processedPixelBuffer != pixelBuffer) {
            CVPixelBufferRelease(processedPixelBuffer);
        }
        CVPixelBufferRelease(pixelBuffer);
    });
}

- (CVPixelBufferRef)createCVPixelBufferFromVideoFrame:(const yffplayer::VideoFrame &)frame {
    // VIDEOTOOLBOX格式直接从mData[3]获取CVPixelBufferRef
    if (frame.mFormat == yffplayer::PixelFormat::VIDEOTOOLBOX) {
        CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)frame.mData[3];
        if (pixelBuffer) {
            CVPixelBufferRetain(pixelBuffer);
            return pixelBuffer;
        } else {
            NSLog(@"VIDEOTOOLBOX format but mData[3] is NULL");
            return NULL;
        }
    }
    
    CVPixelBufferRef pixelBuffer = NULL;
    
    // 根据像素格式创建CVPixelBuffer
    OSType pixelFormat;
    switch (frame.mFormat) {
        case yffplayer::PixelFormat::YUV420P:
            pixelFormat = kCVPixelFormatType_420YpCbCr8Planar;
            break;
        case yffplayer::PixelFormat::NV12:
            pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
            break;
        case yffplayer::PixelFormat::RGB24:
            pixelFormat = kCVPixelFormatType_24RGB;
            break;
        default:
            NSLog(@"Unsupported pixel format: %d", (int)frame.mFormat);
            return NULL;
    }
    
    // 创建像素缓冲区属性
    NSDictionary *pixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(pixelFormat),
        (NSString *)kCVPixelBufferWidthKey: @(frame.mWidth),
        (NSString *)kCVPixelBufferHeightKey: @(frame.mHeight),
        (NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
    
    CVReturn result = CVPixelBufferCreate(kCFAllocatorDefault,
                                         frame.mWidth,
                                         frame.mHeight,
                                         pixelFormat,
                                         (__bridge CFDictionaryRef)pixelBufferAttributes,
                                         &pixelBuffer);
    
    if (result != kCVReturnSuccess) {
        NSLog(@"Failed to create CVPixelBuffer: %d", result);
        return NULL;
    }
    
    // 锁定像素缓冲区
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    
    if (frame.mFormat == yffplayer::PixelFormat::YUV420P) {
        // YUV420P格式：3个平面
        for (int i = 0; i < 3; i++) {
            void *baseAddress = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, i);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, i);
            size_t height = (i == 0) ? frame.mHeight : frame.mHeight / 2;
            
            for (size_t row = 0; row < height; row++) {
                memcpy((uint8_t *)baseAddress + row * bytesPerRow,
                       frame.mData[i] + row * frame.mLinesize[i],
                       (i == 0) ? frame.mWidth : frame.mWidth / 2);
            }
        }
    } else if (frame.mFormat == yffplayer::PixelFormat::NV12) {
        // NV12格式：2个平面
        // Y平面
        void *yBaseAddress = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
        size_t yBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
        for (size_t row = 0; row < frame.mHeight; row++) {
            memcpy((uint8_t *)yBaseAddress + row * yBytesPerRow,
                   frame.mData[0] + row * frame.mLinesize[0],
                   frame.mWidth);
        }
        
        // UV平面
        void *uvBaseAddress = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
        size_t uvBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
        for (size_t row = 0; row < frame.mHeight / 2; row++) {
            memcpy((uint8_t *)uvBaseAddress + row * uvBytesPerRow,
                   frame.mData[1] + row * frame.mLinesize[1],
                   frame.mWidth);
        }
    } else if (frame.mFormat == yffplayer::PixelFormat::RGB24) {
        // RGB24格式：单个平面
        void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
        for (size_t row = 0; row < frame.mHeight; row++) {
            memcpy((uint8_t *)baseAddress + row * bytesPerRow,
                   frame.mData[0] + row * frame.mLinesize[0],
                   frame.mWidth * 3);
        }
    }
    
    // 解锁像素缓冲区
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    return pixelBuffer;
}

- (CVPixelBufferRef)applyColorAdjustmentToPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    // 如果不需要颜色调节，直接返回原始缓冲区
    if (_brightness == 0.0f && _contrast == 1.0f && _saturation == 1.0f) {
        CVPixelBufferRetain(pixelBuffer);
        return pixelBuffer;
    }
    
    // 创建CIImage
    CIImage *inputImage = [CIImage imageWithCVPixelBuffer:pixelBuffer];
    
    // 设置颜色控制滤镜参数
    [_colorControlsFilter setValue:inputImage forKey:kCIInputImageKey];
    [_colorControlsFilter setValue:@(_brightness) forKey:kCIInputBrightnessKey];
    [_colorControlsFilter setValue:@(_contrast) forKey:kCIInputContrastKey];
    [_colorControlsFilter setValue:@(_saturation) forKey:kCIInputSaturationKey];
    
    // 获取输出图像
    CIImage *outputImage = _colorControlsFilter.outputImage;
    if (!outputImage) {
        CVPixelBufferRetain(pixelBuffer);
        return pixelBuffer;
    }
    
    // 创建输出像素缓冲区
    CVPixelBufferRef outputPixelBuffer = NULL;
    
    // 获取输入像素缓冲区的属性
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    
    // 创建像素缓冲区属性
    NSDictionary *pixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(pixelFormat),
        (NSString *)kCVPixelBufferWidthKey: @(width),
        (NSString *)kCVPixelBufferHeightKey: @(height),
        (NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
    
    // 创建输出像素缓冲区
    CVReturn result = CVPixelBufferCreate(kCFAllocatorDefault,
                                         width,
                                         height,
                                         pixelFormat,
                                         (__bridge CFDictionaryRef)pixelBufferAttributes,
                                         &outputPixelBuffer);
    
    if (result != kCVReturnSuccess || !outputPixelBuffer) {
        CVPixelBufferRetain(pixelBuffer);
        return pixelBuffer;
    }
    
    // 渲染到输出像素缓冲区
    [_ciContext render:outputImage toCVPixelBuffer:outputPixelBuffer];
    
    return outputPixelBuffer;
}

- (CMSampleBufferRef)createSampleBufferFromPixelBuffer:(CVPixelBufferRef)pixelBuffer
                                                withPTS:(int64_t)pts
                                               duration:(int64_t)duration {
    CMSampleBufferRef sampleBuffer = NULL;
    
    // 创建视频格式描述
    CMVideoFormatDescriptionRef videoInfo = NULL;
    OSStatus status = CMVideoFormatDescriptionCreateForImageBuffer(NULL, pixelBuffer, &videoInfo);
    if (status != noErr) {
        NSLog(@"Failed to create video format description: %d", (int)status);
        return NULL;
    }
    
    // 创建时间信息
    CMSampleTimingInfo timing = {
        .duration = CMTimeMakeWithSeconds(duration / 1000.0, 1000),
        .presentationTimeStamp = CMTimeMakeWithSeconds(pts / 1000.0, 1000),
        .decodeTimeStamp = kCMTimeInvalid
    };
    
    // 创建CMSampleBuffer
    status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault,
                                               pixelBuffer,
                                               true,
                                               NULL,
                                               NULL,
                                               videoInfo,
                                               &timing,
                                               &sampleBuffer);
    
    CFRelease(videoInfo);
    
    if (status != noErr) {
        NSLog(@"Failed to create sample buffer: %d", (int)status);
        return NULL;
    }
    
    return sampleBuffer;
}

#pragma mark - Color Adjustment Properties

- (void)setBrightness:(float)brightness {
    _brightness = MAX(-1.0f, MIN(1.0f, brightness));
}

- (float)brightness {
    return _brightness;
}

- (void)setContrast:(float)contrast {
    _contrast = MAX(0.0f, MIN(2.0f, contrast));
}

- (float)contrast {
    return _contrast;
}

- (void)setSaturation:(float)saturation {
    _saturation = MAX(0.0f, MIN(2.0f, saturation));
}

- (float)saturation {
    return _saturation;
}

- (void)dealloc {
    [_displayLayer removeFromSuperlayer];
}

@end
