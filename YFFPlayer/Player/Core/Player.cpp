#include "Player.h"
#include <chrono>
#include <thread>

#if TARGET_OS_IOS
#include <pthread.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace yffplayer {
Player::Player(std::shared_ptr<AudioOutput> audioOutput, std::shared_ptr<VideoOutput> videoOutput)
    : mAudioOutput(audioOutput), mVideoOutput(videoOutput),
      mAudioPacketQueue(std::make_shared<PacketQueue>(100)),
      mVideoPacketQueue(std::make_shared<PacketQueue>(50)),
      mAudioFrameQueue(std::make_shared<FrameQueue<AudioFrame>>(100)),
      mVideoFrameQueue(std::make_shared<FrameQueue<VideoFrame>>(50)) {
    mDemuxer = std::make_shared<Demuxer>(mAudioPacketQueue, mVideoPacketQueue);
}

Player::~Player() {
    stop();
}

bool Player::open(const std::string& url, MediaInfo& mediaInfo) {
    if (!mDemuxer->open(url, mediaInfo)) {
        return false;
    }
    mMediaInfo = mediaInfo;

    if (mMediaInfo.mHasAudio) {
        AVCodecParameters* audioCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(audioCodecParams, mMediaInfo.mAudioCodecParameters);
        mAudioDecoder = std::make_shared<AudioDecoder>(mAudioPacketQueue, mAudioFrameQueue);
        mAudioDecoder->open(audioCodecParams, mMediaInfo.mAudioTimeBase);
        avcodec_parameters_free(&audioCodecParams);

        if (!mAudioOutput->initialize(mMediaInfo.mAudioSampleRate, mMediaInfo.mAudiochannels, 4096 * 2 * 2, shared_from_this())) {
            return false;
        }
    }

    if (mMediaInfo.mHasVideo) {
        AVCodecParameters* videoCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(videoCodecParams, mMediaInfo.mVideoCodecParameters);
        mVideoDecoder = std::make_shared<VideoDecoder>(mVideoPacketQueue, mVideoFrameQueue);
        mVideoDecoder->open(videoCodecParams, mMediaInfo.mVideoTimeBase);
        avcodec_parameters_free(&videoCodecParams);

        if (!mVideoOutput->initialize(mMediaInfo.mVideoWidth, mMediaInfo.mVideoHeight)) {
            return false;
        }
    }

    return true;
}

void Player::start() {
    mRunning = true;
    mDemuxer->start();
    if (mMediaInfo.mHasAudio) {
        mAudioDecoder->start();
        mAudioRenderThread = std::thread(&Player::audioRenderThread, this);
    }
    if (mMediaInfo.mHasVideo) {
        mVideoDecoder->start();
        mVideoRenderThread = std::thread(&Player::videoRenderThread, this);
    }
}

void Player::stop() {
    mRunning = false;
    if (mAudioOutput) {
        mAudioOutput->stop();
    }
    if (mVideoOutput) {
        mVideoOutput->stop();
    }
    if (mAudioRenderThread.joinable()) {
        mAudioRenderThread.join();
    }
    if (mVideoRenderThread.joinable()) {
        mVideoRenderThread.join();
    }
    if (mAudioDecoder) {
        mAudioDecoder->stop();
    }
    if (mVideoDecoder) {
        mVideoDecoder->stop();
    }
    mDemuxer->stop();
}

void Player::audioRenderThread() {
    mAudioOutput->start();
}

void Player::videoRenderThread() {
#if TARGET_OS_IOS
    pthread_setname_np("com.yffplayer.video_render");
#endif
    while (mRunning) {
        auto videoFrame = mVideoFrameQueue->pop();
        if (videoFrame) {
            int64_t pts = videoFrame->mPts;
            if (mMediaInfo.mHasAudio) {
                int64_t audioClock = mAudioClock.load();
                int64_t diff = pts - audioClock;
                if (diff > 50) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(diff));
                    mVideoOutput->renderVideoFrame(*videoFrame);
                } else if (diff < -50) {
                    continue; // 跳过落后太多的帧
                } else {
                    mVideoOutput->renderVideoFrame(*videoFrame);
                }
            } else {
                mVideoOutput->renderVideoFrame(*videoFrame);
                int64_t frameDuration = videoFrame->mDuration;
                if (frameDuration <= 0) {
                    frameDuration = 33; // 默认 30fps
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(frameDuration));
            }
        }
    }
}

std::shared_ptr<AudioFrame> Player::getNextAudioFrame() {
    auto audioFrame = mAudioFrameQueue->pop();
    if (!audioFrame) {
        return nullptr; // 如果队列为空，返回nullptr
    }
    mAudioClock = audioFrame->mPts + audioFrame->mDuration; // 更新音频时钟
    return audioFrame;
}
} // namespace yffplayer
