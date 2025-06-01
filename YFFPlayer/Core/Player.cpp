#include "Player.h"

#include <chrono>
#include <iostream>
#include <thread>

#if defined(__APPLE__)
#include <pthread.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace yffplayer {
Player::Player(std::shared_ptr<AudioOutput> audioOutput, std::shared_ptr<VideoOutput> videoOutput,
               std::shared_ptr<PlayerCallback> callback)
    : mAudioOutput(audioOutput),
      mVideoOutput(videoOutput),
      mCallback(callback),
      mSyncManager(std::make_unique<SyncManager>()),
      mAudioPacketQueue(std::make_shared<PacketQueue>(100)),
      mVideoPacketQueue(std::make_shared<PacketQueue>(50)),
      mAudioFrameQueue(std::make_shared<FrameQueue<AudioFrame>>(100)),
      mVideoFrameQueue(std::make_shared<FrameQueue<VideoFrame>>(50)),
      mAudioProcessor(std::make_unique<SonicAudioProcessor>()) {
    mDemuxer = std::make_unique<Demuxer>(mAudioPacketQueue, mVideoPacketQueue);
}

Player::~Player() { stop(); }

bool Player::open(const std::string& url, MediaInfo& mediaInfo) {
    stop();
    mDemuxer->setCallback(shared_from_this());
    if (!mDemuxer->open(url, mediaInfo)) {
        return false;
    }
    mMediaInfo = mediaInfo;
    if (mMediaInfo.mHasAudio) {
        AVCodecParameters* audioCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(audioCodecParams, mMediaInfo.mAudioCodecParameters);
        mAudioDecoder = std::make_unique<AudioDecoder>(mAudioPacketQueue, mAudioFrameQueue);
        mAudioDecoder->open(audioCodecParams, mMediaInfo.mAudioTimeBase);
        avcodec_parameters_free(&audioCodecParams);
        if (!mAudioOutput->init(44100, 2)) {
            return false;
        }
    }

    if (mMediaInfo.mHasVideo) {
        AVCodecParameters* videoCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(videoCodecParams, mMediaInfo.mVideoCodecParameters);
        mVideoDecoder = std::make_unique<VideoDecoder>(mVideoPacketQueue, mVideoFrameQueue);
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
        mAudioOutputThread = std::thread(&Player::audioOutputThread, this);
    }
    if (mMediaInfo.mHasVideo) {
        mVideoDecoder->start();
        mVideoOutputThread = std::thread(&Player::videoOutputThread, this);
    }
}

void Player::stop() {
    if (!mRunning) {
        return;
    }
    mRunning = false;
    mPaused = false;
    mDroppedVideoFramesCount = 0;

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
    if (mAudioOutputThread.joinable()) {
        mAudioOutputThread.join();
    }

    // 停止视频输出，结束视频渲染线程
    if (mVideoOutput) {
        mVideoOutput->stop();
    }
    if (mVideoOutputThread.joinable()) {
        mVideoOutputThread.join();
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

void Player::notifyProgressChanged() {
    if (mCallback) {
        mCallback->onProgress(mSyncManager->getClock(), mMediaInfo.mDurationMs);
    }
}

void Player::pause() {
    if (!mRunning || mPaused) {
        return;  // 已经暂停或未运行
    }
    mPaused = true;

    // 停止解复用和解码线程继续读取数据
    mDemuxer->pause();
    if (mAudioDecoder) {
        mAudioDecoder->pause();
        mAudioPacketQueue->abort(); 
        mAudioFrameQueue->abort();
    }
    if (mVideoDecoder) {
        mVideoDecoder->pause();
        mVideoPacketQueue->abort();
        mVideoFrameQueue->abort();
    }

    // 暂停音频输出
    mAudioOutput->pause();
}

void Player::resume() {
    if (!mRunning || !mPaused) {
        return;  // 未暂停或未运行
    }
    mPaused = false;

    // 解除abort状态，允许继续读取和解码
    mDemuxer->resume();
    if (mAudioDecoder) {
        mAudioPacketQueue->start();
        mAudioFrameQueue->start();
        mAudioDecoder->resume();
    }
    if (mVideoDecoder) {
        mVideoPacketQueue->start();
        mVideoFrameQueue->start();
        mVideoDecoder->resume();
    }

    // 恢复音频输出
    mAudioOutput->resume();
}

bool Player::seek(int64_t positionMs) {
    if (!mRunning) return false;

    pause();  // 暂停所有模块，避免数据冲突

    // 设置所有队列进入 abort 状态以终止阻塞
    mAudioPacketQueue->abort();
    mVideoPacketQueue->abort();
    mAudioFrameQueue->abort();
    mVideoFrameQueue->abort();

    // 清空队列数据
    mAudioPacketQueue->clear();
    mVideoPacketQueue->clear();
    mAudioFrameQueue->clear();
    mVideoFrameQueue->clear();

    // 通知解复用器跳转
    if (!mDemuxer->seek(positionMs)) {
        resume();  // 恢复播放状态
        return false;
    }

    // 刷新解码器内部状态（丢弃之前缓冲的帧）
    if (mAudioDecoder) mAudioDecoder->flush();
    if (mVideoDecoder) mVideoDecoder->flush();

    // 重置时钟（以便同步）
    mSyncManager->updateClock(positionMs, 0);

    // 音视频输出模块同步清理（如果有缓存）
    mAudioProcessor->flush();
    mAudioOutput->flush();
    //    mVideoOutput->flush();

    // 解除 abort 状态，恢复继续解码和播放
    mAudioPacketQueue->start();
    mVideoPacketQueue->start();
    mAudioFrameQueue->start();
    mVideoFrameQueue->start();

    resume();  // 恢复播放
    return true;
}

void Player::audioOutputThread() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.output.audio");
#endif

    if (mAudioProcessor && mMediaInfo.mHasAudio) {
        if (!mAudioProcessor->initialize(mMediaInfo.mAudioSampleRate, mMediaInfo.mAudioChannels)) {
            std::cerr << "Failed to initialize audio processor" << std::endl;
            return;
        }
        mAudioProcessor->setPlaybackRate(mPlaybackRate.load());
    }

    mAudioOutput->setPlaybackCallback([this](int64_t pts, int64_t duration) {
        mSyncManager->updateClock(pts, duration);
        notifyProgressChanged();
    });

    mAudioOutput->start();
    while (mRunning) {
        if (mPaused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto audioFrame = mAudioFrameQueue->pop();
        if (audioFrame) {
            // 修改这里的判断条件
            if (mAudioProcessor && std::abs(mPlaybackRate.load() - 1.0f) > 0.01f) {
                auto processedFrame = mAudioProcessor->processAudioFrame(*audioFrame);
                if (processedFrame) {
                    mAudioOutput->enqueueAudioFrame(*processedFrame);
                }
            } else {
                mAudioOutput->enqueueAudioFrame(*audioFrame);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    mAudioOutput->stop();
}

void Player::videoOutputThread() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.output.video");
#endif
    while (mRunning) {
        if (mPaused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto videoFrame = mVideoFrameQueue->pop();
        if (videoFrame) {
            int64_t pts = videoFrame->mPts;
            float playbackRate = mPlaybackRate.load();
            if (mMediaInfo.mHasAudio) {
                bool shouldDropFrame = false;
                int64_t delay = mSyncManager->calculateDelay(pts, shouldDropFrame);
                if (shouldDropFrame) {
                    ++mDroppedVideoFramesCount;
                    std::cerr << "Dropped video frame, total count: " << mDroppedVideoFramesCount
                              << std::endl;
                    continue;
                } else {
                    int ret = av_usleep(static_cast<unsigned int>(delay * 1000));
                    if (ret != 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                    }
                    mVideoOutput->renderVideoFrame(*videoFrame);
                }
            } else {
                mVideoOutput->renderVideoFrame(*videoFrame);
                int64_t frameDuration = videoFrame->mDuration;
                if (frameDuration <= 0) {
                    frameDuration = mMediaInfo.mVideoFrameRate;
                    if (frameDuration <= 0) {
                        frameDuration = 30;
                    }
                }
                int64_t adjustedDuration = static_cast<int64_t>(frameDuration / playbackRate);
                mSyncManager->updateClock(pts, frameDuration);
                notifyProgressChanged();
                int ret = av_usleep(static_cast<unsigned int>(adjustedDuration * 1000));
                if (ret != 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(adjustedDuration));
                }
            }
        }
    }
}

void Player::setPlaybackRate(float rate) {
    if (rate <= 0.0f) return;
    if (rate == mPlaybackRate) return;

    rate = std::max(0.25f, std::min(4.0f, rate));
    mSyncManager->setSpeed(rate);

    if (mAudioProcessor) {
        mAudioProcessor->flush();
        mAudioProcessor->setPlaybackRate(rate);
    }
    mPlaybackRate.store(rate);
    mAudioOutput->flush();
}

float Player::getPlaybackRate() const { return mPlaybackRate.load(); }

void Player::onDemuxStarted() {}
void Player::onDemuxPaused() {}
void Player::onDemuxResumed() {}
void Player::onDemuxStopped() {}
void Player::onReadError(const Error& error) {}
void Player::onEndOfFile() {}
void Player::onNetworkError(const Error& error) {}
void Player::onSeekStarted(int64_t targetTimestampMs) {}
void Player::onSeekCompleted(int64_t actualTimestampMs) {}
void Player::onSeekFailed(int64_t targetTimestampMs, const Error& error) {}
void Player::onDemuxProgress(int64_t currentTimestampMs, int64_t durationMs) {}

}  // namespace yffplayer
