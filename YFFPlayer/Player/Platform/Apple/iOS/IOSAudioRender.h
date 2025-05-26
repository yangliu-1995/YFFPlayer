#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <memory>
#include "AudioFrame.h"

@class IOSAudioRender;

typedef void (^IOSAudioRenderFrameProvider)(IOSAudioRender *renderer);

@interface IOSAudioRender : NSObject

- (instancetype)initWithSampleRate:(int)sampleRate
                          channels:(int)channels
                        frameBytes:(UInt32)frameBytes
                         frameProvider:(IOSAudioRenderFrameProvider)provider;

- (void)start;
- (void)stop;

- (void)feedAudioFrame:(const std::shared_ptr<yffplayer::AudioFrame> &)frame;

@end
