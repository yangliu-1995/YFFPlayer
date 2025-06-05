#include "Player.h"

#include <chrono>
#include <thread>

#include "AudioResampleContext.h"
#include "Log.h"

#if defined(__APPLE__)
#include <pthread.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}

#define AV_SYNC_THRESHOLD_MIN 0.04

#define AV_SYNC_THRESHOLD_MAX 0.1

namespace yffplayer {
Player::Player(std::shared_ptr<AudioOutput> audioOutput, std::shared_ptr<VideoOutput> videoOutput,
               std::shared_ptr<PlayerCallback> callback)
    : mAudioOutput(audioOutput),
      mVideoOutput(videoOutput),
      mCallback(callback),
      mAudioPacketQueue(std::make_shared<PacketQueue>(100)),
      mVideoPacketQueue(std::make_shared<PacketQueue>(50)),
      mAudioFrameQueue(std::make_shared<FrameQueue<FrameHandle>>(100)),
      mVideoFrameQueue(std::make_shared<FrameQueue<FrameHandle>>(50)) {
    mDemuxer = std::make_unique<Demuxer>(mAudioPacketQueue, mVideoPacketQueue);
    //    mSyncManager->setSyncMode(SyncMode::EXTERNAL_CLOCK);
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
        mAudioProcessor = std::make_unique<AudioFrameProcessor>();
        if (!mAudioProcessor->initialize(mAudioDecoder->getSampleRate(),
                                         mAudioDecoder->getNbChannels(),
                                         mAudioDecoder->getFormat())) {
            return false;
        }
        if (!mAudioOutput->init(48000, 2)) {
            return false;
        }
    }

    if (mMediaInfo.mHasVideo) {
        AVCodecParameters* videoCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(videoCodecParams, mMediaInfo.mVideoCodecParameters);
        mVideoDecoder = std::make_unique<VideoDecoder>(mVideoPacketQueue, mVideoFrameQueue);
        mVideoDecoder->open(videoCodecParams, mMediaInfo.mVideoTimeBase);
        avcodec_parameters_free(&videoCodecParams);
        if (!mVideoProcessor) {
            mVideoProcessor = std::make_unique<VideoFrameProcessor>();
        }
        if (!mVideoOutput->initialize(mMediaInfo.mVideoWidth, mMediaInfo.mVideoHeight)) {
            return false;
        }
    }

    if (mMediaInfo.mIsLiveStream) {
        mSyncManager = std::make_unique<SyncManager>(SyncManager::SyncType::External);
    } else {
        if (mMediaInfo.mHasAudio) {
            mSyncManager = std::make_unique<SyncManager>(SyncManager::SyncType::Audio);
        } else {
            if (mMediaInfo.mHasVideo) {
                mSyncManager = std::make_unique<SyncManager>(SyncManager::SyncType::Video);
            }
        }
    }
    mSyncManager = std::make_unique<SyncManager>(SyncManager::SyncType::External);
    return true;
}

void Player::start() {
    mRunning = true;
    mRequiresSyncClock = true;
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
        mCallback->onProgress(mSyncManager->getClockTime(), mMediaInfo.mDurationMs);
    }
}

void Player::syncClockIfNeeded(int64_t pts) {
    if (mRequiresSyncClock.load()) {
        //        mSyncManager->updateTime(pts / 1000.0);
        mRequiresSyncClock = false;
    }
}

void Player::pause() {
    if (!mRunning || mPaused) {
        return;  // 已经暂停或未运行
    }
    mPaused = true;

    // 暂停外部时钟
    if (mSyncManager) {
        mSyncManager->pause();
    }

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

    if (mSyncManager) {
        mSyncManager->resume();
    }

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
        mAudioProcessor->setPlaybackRate(mPlaybackRate.load());
    }

    if (mMediaInfo.mHasAudio) {
        mAudioFrameQueue->wait_for_frames(3);
    }

    mAudioPlaybackRateDelt = 0;
    mAudioOutput->setPlaybackCallback([this](int64_t pts, int64_t duration) {
        int64_t adjustedDuration = duration / (mPlaybackRate + mAudioPlaybackRateDelt);
        mSyncManager->updateAudioTime(pts / 1000.0, adjustedDuration / 1000.0);
        double delay = mSyncManager->computeAudioTargetDelay((pts + adjustedDuration) / 1000.0);
        LogInfo << "audio delay: " << delay;

        double absDelay = fabs(delay);
        double sign = (delay > 0) ? 1.0 : -1.0;

        if (absDelay < 0.010) {
            mAudioPlaybackRateDelt = 0.0;
        } else if (absDelay < 0.030) {
            mAudioPlaybackRateDelt = 0.01 * sign;
        } else if (absDelay < 0.050) {
            mAudioPlaybackRateDelt = 0.03 * sign;
        } else {
            mAudioPlaybackRateDelt = 0.05 * sign;
        }

        if (mAudioProcessor) {
            mAudioProcessor->setPlaybackRate(mPlaybackRate + mAudioPlaybackRateDelt);
        }
        notifyProgressChanged();
    });

    mAudioOutput->start();

    auto frameHandle = mAudioFrameQueue->back();
    if (frameHandle) {
        auto frame = frameHandle->getFrame();
        if (frame) {
            int64_t pts = frame->pts;
            int64_t duration = frame->duration;
            duration /= mPlaybackRate;
            mLastVideoPts = pts;
            mSyncManager->initAudioClock((pts + duration) / 1000.0);
        }
    }

    while (mRunning) {
        if (mPaused) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }
        auto frameHandle = mAudioFrameQueue->pop();
        if (frameHandle) {
            auto audioFrame = mAudioProcessor->processAudioFrame(frameHandle, 0);
            if (audioFrame) {
                mAudioOutput->enqueueAudioFrame(*audioFrame);
            } else {
                av_usleep(static_cast<unsigned int>(10 * 1000));
            }
        } else {
            av_usleep(static_cast<unsigned int>(10 * 1000));
        }
    }
    mAudioOutput->stop();
}

void Player::videoOutputThread() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.output.video");
#endif

    mVideoFrameQueue->wait_for_frames(3);
    auto frameHandle = mVideoFrameQueue->back();
    if (frameHandle) {
        auto frame = frameHandle->getFrame();
        if (frame) {
            int64_t pts = frame->pts;
            mLastVideoPts = pts;
            mSyncManager->initVideoClock(pts / 1000.0);
        }
    }
    while (mRunning) {
        if (mFrameTimer <= 0.0) {
            mFrameTimer = av_gettime_relative() / 1000000.0;  // 获取当前时间戳（秒）
        }
        if (mPaused) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }
        auto frameHandle = mVideoFrameQueue->pop();
        if (!frameHandle) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }
        auto videoFrame = mVideoProcessor->processAudioFrame(frameHandle);

        if (videoFrame) {
            int64_t pts = videoFrame->mPts;
            double lastDuration = pts - mLastVideoPts;
            double delay = mSyncManager->computeVideoTargetDelay(lastDuration / 1000.0);
            double time = av_gettime_relative() / 1000000.0;
            double sleepTime = delay;
            LogInfo << "last duration: " << lastDuration << ", sleep time: " << sleepTime
                   ;
            if (time < mFrameTimer + delay) {
                sleepTime = FFMIN(mFrameTimer + delay - time, sleepTime);
                sleepTime = FFMAX(sleepTime, 0.0);
                av_usleep(static_cast<unsigned int>(sleepTime * 1000 * 1000));
            }
            mVideoOutput->renderVideoFrame(*videoFrame);
            mFrameTimer += delay;
            if (delay > 0 && time - mFrameTimer > AV_SYNC_THRESHOLD_MAX) mFrameTimer = time;
            mLastVideoPts = pts;
            mSyncManager->updateVideoTime(pts / 1000.0);
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
