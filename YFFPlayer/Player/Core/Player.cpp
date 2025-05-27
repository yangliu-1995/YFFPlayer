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
    stop();
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
    if (!mRunning) {
        return;
    }
    mRunning = false;
    mPaused = false;

    // 先中断所有队列阻塞
    if (mAudioPacketQueue) {
        mAudioPacketQueue->abort();
        mAudioPacketQueue->clear();
    }
    if (mVideoPacketQueue) {
        mVideoPacketQueue->abort();
        mVideoPacketQueue->clear();
    }
    if (mAudioFrameQueue) {
        mAudioFrameQueue->abort();
        mAudioFrameQueue->clear();
    }
    if (mVideoFrameQueue) {
        mVideoFrameQueue->abort();
        mVideoFrameQueue->clear();
    }

    // 停止解码器，解码器中如果阻塞了pop，也会因队列abort返回
    if (mAudioDecoder) {
        mAudioDecoder->stop();
    }
    if (mVideoDecoder) {
        mVideoDecoder->stop();
    }

    // 停止音频输出，结束音频渲染线程
    if (mAudioOutput) {
        mAudioOutput->stop();
    }
    if (mAudioRenderThread.joinable()) {
        mAudioRenderThread.join();
    }

    // 停止视频输出，结束视频渲染线程
    if (mVideoOutput) {
        mVideoOutput->stop();
    }
    if (mVideoRenderThread.joinable()) {
        mVideoRenderThread.join();
    }

    // 停止解复用线程
    if (mDemuxer) {
        mDemuxer->stop();
    }

    // 恢复队列可用状态，以备重新open时使用
    if (mAudioPacketQueue) {
        mAudioPacketQueue->start();
    }
    if (mVideoPacketQueue) {
        mVideoPacketQueue->start();
    }
    if (mAudioFrameQueue) {
        mAudioFrameQueue->start();
    }
    if (mVideoFrameQueue) {
        mVideoFrameQueue->start();
    }
}


void Player::stopThread() {
    // 原stop方法的主体逻辑移到这里

}

void Player::pause() {
    if (!mRunning || mPaused) {
        return; // 已经暂停或未运行
    }
    mPaused = true;
    mDemuxer->pause();
    if (mAudioDecoder) {
        mAudioDecoder->pause();
    }
    if (mVideoDecoder) {
        mVideoDecoder->pause();
    }
}

void Player::resume() {
    if (!mRunning || !mPaused) {
        return; // 未暂停或未运行
    }
    mPaused = false;
    mDemuxer->resume();
    if (mAudioDecoder) {
        mAudioDecoder->resume();
    }
    if (mVideoDecoder) {
        mVideoDecoder->resume();
    }
}

void Player::audioRenderThread() {
    mAudioOutput->start();
}

void Player::videoRenderThread() {
#if TARGET_OS_IOS
    pthread_setname_np("com.yffplayer.video_render");
#endif
    while (mRunning) {
        // 如果暂停状态，则等待一小段时间后继续检查
        if (mPaused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        auto videoFrame = mVideoFrameQueue->pop();
        if (videoFrame) {
            int64_t pts = videoFrame->mPts;
            if (mMediaInfo.mHasAudio) {
                int64_t audioClock = mAudioClock.load();
                int64_t diff = pts - audioClock;
                if (diff > 50) {
                    int ret = av_usleep(static_cast<unsigned int>(diff * 1000));
                    if (ret != 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(diff));
                    }
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
                int ret = av_usleep(static_cast<unsigned int>(frameDuration * 1000));
                if (ret != 0) {
                    // 处理中断
                    std::this_thread::sleep_for(std::chrono::milliseconds(frameDuration));
                }
            }
        }
    }
}

std::shared_ptr<AudioFrame> Player::getNextAudioFrame() {
    // 如果播放器处于暂停状态，不返回音频帧
    if (mPaused) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return nullptr;
    }
    
    auto audioFrame = mAudioFrameQueue->pop();
    if (!audioFrame) {
        return nullptr; // 如果队列为空，返回nullptr
    }
    mAudioClock = audioFrame->mPts + audioFrame->mDuration; // 更新音频时钟
    return audioFrame;
}
} // namespace yffplayer
