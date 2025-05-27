#include "IOSAudioOutput.h"
#include "AudioFrame.h"

namespace yffplayer {
IOSAudioOutput::IOSAudioOutput() {
}

IOSAudioOutput::~IOSAudioOutput() {
}

bool IOSAudioOutput::initialize(int sampleRate, int channels, int frameBytes, std::shared_ptr<AudioOutputFrameProvider> frameProvider) {
    mFrameProvider = frameProvider;
    std::weak_ptr<IOSAudioOutput> weakThis = shared_from_this(); // 获取 this 的弱引用
    mAudioRender = [[IOSAudioRender alloc] initWithSampleRate:sampleRate
                                                     channels:channels
                                                   frameBytes:frameBytes
                                                frameProvider:^(IOSAudioRender* renderer) {
        auto strongThis = weakThis.lock();
        if (strongThis) {
            auto frameProvider = strongThis->mFrameProvider.lock();
            if (frameProvider) {
                auto audioFramePtr = frameProvider->getNextAudioFrame();
                [renderer feedAudioFrame:audioFramePtr];
            }
        }
    }];
    return mAudioRender != nil;
}

void IOSAudioOutput::start() {
    [mAudioRender start];
}

void IOSAudioOutput::feedAudioFrame(const AudioFrame& frame) {
    [mAudioRender feedAudioFrame:std::make_shared<AudioFrame>(frame)];
}

void IOSAudioOutput::stop() {
    [mAudioRender stop];
}
} // namespace yffplayer
