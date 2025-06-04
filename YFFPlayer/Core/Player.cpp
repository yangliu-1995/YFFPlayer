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
      mVideoFrameQueue(std::make_shared<FrameQueue<VideoFrame>>(50)),
      mAudioProcessor(std::make_unique<AudioProcessor>()) {
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
    mRequiresSyncClock = true;  // 重置漂移同步标记
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
//        mSyncManager->pauseExternalClock();
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

    // 恢复外部时钟
    if (mSyncManager) {
        mSyncManager->resume();
    }

    mRequiresSyncClock = true;

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
    
    // 标记下一帧为首帧，用于更新漂移补偿
    mRequiresSyncClock = true;

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

    static float targetSpeed = mPlaybackRate;

    mAudioOutput->setPlaybackCallback([this](int64_t pts, int64_t duration) {
        mSyncManager->updateAudioTime(pts / 1000.0, duration / 1000.0);
//        double delay = mSyncManager->computeAudioFrameDelay((pts + duration) / 1000.0);
//        std::cerr<<"debug pts audio: "<<pts+duration<<std::endl;
        syncClockIfNeeded(pts);
        notifyProgressChanged();
    });

    mAudioOutput->start();
    
    while (mRunning) {
        if (mPaused) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }

        auto frameHandle = mAudioFrameQueue->pop();
        if (frameHandle) {
            // 使用AudioProcessor将FrameHandle转换为AudioFrame
            auto audioFrame = mAudioProcessor->processFrameHandle(*frameHandle);
            if (audioFrame) {
                if (mAudioProcessor && std::abs(mPlaybackRate.load() - 1.0f) > 0.01f) {
                    // 已经在processFrameHandle中处理了播放倍率
                    mAudioOutput->enqueueAudioFrame(*audioFrame);
                } else {
                    int wanted_samples = synchronizeAudio(audioFrame);
                    if (wanted_samples != audioFrame->mNbSamples) {
                        std::cerr<<"wanted_samples: "<<wanted_samples<<", orignal samples: " << audioFrame->mNbSamples <<std::endl;
                        auto processedFrame = mAudioProcessor->resampleToWantedSamples(*audioFrame, wanted_samples);
                        if (processedFrame) {
                            audioFrame = std::move(processedFrame);
                        }
                        mAudioOutput->enqueueAudioFrame(*audioFrame);
                    } else {
                        mAudioOutput->enqueueAudioFrame(*audioFrame);
                    }
                }
            }
        } else {
            av_usleep(static_cast<unsigned int>(1 * 1000));
        }
    }
    mAudioOutput->stop();
}

int Player::synchronizeAudio(std::shared_ptr<AudioFrame> frame) {
    const int avgWindowSize = 20;
    const double audioDiffAvgCoef = 0.95;
    const double diffThreshold = 0.05; // 50ms
    const int maxAdjustPercent = 5;

    static double audioDiffCum = 0;
    static int audioDiffCount = 0;
    int nb_samples = frame->mNbSamples;
    int wanted_samples = nb_samples;
    
    double diff = mSyncManager->computeAudioFrameDelay(frame->mPts / 1000.0);
    std::cerr << "audio diff: " << diff << std::endl;
    
    if (!std::isnan(diff) && fabs(diff) < 10) {
        audioDiffCum = diff + audioDiffAvgCoef * audioDiffCum;
        
        if (audioDiffCount < avgWindowSize) {
            audioDiffCount++;
        } else {
            double avgDiff = audioDiffCum * (1.0 - audioDiffAvgCoef);
            if (fabs(avgDiff) > diffThreshold) {
                int sampleRate = frame->mSampleRate;
                // 修复：音频领先时增加采样数来减慢播放，滞后时减少采样数来加快播放
                wanted_samples = nb_samples + static_cast<int>(avgDiff * sampleRate);
                int min_samples = nb_samples * (100 - maxAdjustPercent) / 100;
                int max_samples = nb_samples * (100 + maxAdjustPercent) / 100;
                wanted_samples = std::clamp(wanted_samples, min_samples, max_samples);
                
                std::cerr << "[audioSync] avgDiff=" << avgDiff
                << ", wanted_samples=" << wanted_samples
                << ", original=" << nb_samples << std::endl;
            }
        }
    } else {
        audioDiffCount = 0;
        audioDiffCum = 0;
    }
    
    return wanted_samples;
}

void Player::videoOutputThread() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.output.video");
#endif
    while (mRunning) {
        if (mPaused) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }

        auto videoFrame = mVideoFrameQueue->pop();
        if (videoFrame) {
            int64_t pts = videoFrame->mPts;
            float playbackRate = mPlaybackRate.load();
            if (mMediaInfo.mHasAudio) {
//                double delay = FFMAX(mSyncManager->computeVideoFrameDelay(pts / 1000.0), 0);
                double delay = mSyncManager->computeVideoFrameDelay(pts / 1000.0);
                double sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
                std::cerr<< "sync_threshold: " << sync_threshold << std::endl;
                std::cerr << "video delay: " << delay << std::endl;
                if (fabs(delay) > 0.05) {
                    mSyncManager->updateClock((pts) / 1000.0);
                    continue;
                }
                if (delay > sync_threshold) {
                    ++mDroppedVideoFramesCount;
//                    av_usleep(static_cast<unsigned int>(videoFrame->mDuration * 1000));
                    mSyncManager->updateClock((pts) / 1000.0);
                    continue;
                } else {

                    av_usleep(static_cast<unsigned int>(delay * 1000 * 1000));
                    mVideoOutput->renderVideoFrame(*videoFrame);
                    mSyncManager->updateClock((pts) / 1000.0);
                }
                std::cerr<<"debug pts video: "<<pts<<"\n"<<std::endl;
//                syncClockIfNeeded(pts);
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
//                mSyncManager->updateVideoTime(pts / 1000.0);
                notifyProgressChanged();
                av_usleep(static_cast<unsigned int>(adjustedDuration * 1000));
            }
        }
    }
}

void Player::setPlaybackRate(float rate) {
    if (rate <= 0.0f) return;
    if (rate == mPlaybackRate) return;

    rate = std::max(0.25f, std::min(4.0f, rate));
    mRequiresSyncClock = true;
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
