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
    : audioOutput_(audioOutput),
      videoOutput_(videoOutput),
      callback_(callback),
      audioPacketQueue_(std::make_shared<PacketQueue>(100)),
      videoPacketQueue_(std::make_shared<PacketQueue>(50)),
      audioFrameQueue_(std::make_shared<FrameQueue<FrameHandle>>(100)),
      videoFrameQueue_(std::make_shared<FrameQueue<FrameHandle>>(50)) {
    demuxer_ = std::make_unique<Demuxer>(audioPacketQueue_, videoPacketQueue_);
    Log::redirectFFmpegLog();
}

Player::~Player() { stop(); }

bool Player::open(const std::string& url, MediaInfo& mediaInfo) {
    stop();
    demuxer_->setCallback(shared_from_this());
    if (!demuxer_->open(url, mediaInfo)) {
        return false;
    }
    mediaInfo_ = mediaInfo;
    if (mediaInfo_.hasAudio_) {
        AVCodecParameters* audioCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(audioCodecParams, mediaInfo_.audioCodecParameters_);
        audioDecoder_ = std::make_unique<AudioDecoder>(audioPacketQueue_, audioFrameQueue_);
        audioDecoder_->open(audioCodecParams, mediaInfo_.audioTimeBase_);
        avcodec_parameters_free(&audioCodecParams);
        audioProcessor_ = std::make_unique<AudioFrameProcessor>();
        if (!audioProcessor_->initialize(audioDecoder_->getSampleRate(),
                                         audioDecoder_->getNbChannels(),
                                         audioDecoder_->getFormat())) {
            return false;
        }
        if (!audioOutput_->init(48000, 2)) {
            return false;
        }
    }

    if (mediaInfo_.hasVideo_) {
        AVCodecParameters* videoCodecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(videoCodecParams, mediaInfo_.videoCodecParameters_);
        videoDecoder_ = std::make_unique<VideoDecoder>(videoPacketQueue_, videoFrameQueue_);
        videoDecoder_->open(videoCodecParams, mediaInfo_.videoTimeBase_);
        avcodec_parameters_free(&videoCodecParams);
        if (!videoProcessor_) {
            videoProcessor_ = std::make_unique<VideoFrameProcessor>();
        }
        if (!videoOutput_->initialize(mediaInfo_.videoWidth_, mediaInfo_.videoHeight_, mediaInfo_.videoFrameRate_)) {
            return false;
        }
    }

    if (mediaInfo_.isLiveStream_) {
        syncManager_ = std::make_unique<SyncManager>(SyncManager::SyncType::External);
    } else {
        if (mediaInfo_.hasAudio_) {
            syncManager_ = std::make_unique<SyncManager>(SyncManager::SyncType::Audio);
        } else {
            if (mediaInfo_.hasVideo_) {
                syncManager_ = std::make_unique<SyncManager>(SyncManager::SyncType::Video);
            }
        }
    }
    syncManager_->setMaxFrameDuration(mediaInfo_.isTsDiscont_ ? 10.0 : 3600.0);
    // Just for testing, use external sync manager
    syncManager_ = std::make_unique<SyncManager>(SyncManager::SyncType::External);
    return true;
}

void Player::start() {
    running_ = true;
    requiresSyncClock_ = true;
    lastVideoPts_ = NAN;
    demuxer_->start();
    if (mediaInfo_.hasAudio_) {
        audioDecoder_->start();
        audioOutputThread_ = std::thread(&Player::audioOutputThread, this);
    }
    if (mediaInfo_.hasVideo_) {
        videoDecoder_->start();
        videoOutputThread_ = std::thread(&Player::videoOutputThread, this);
    }
}

void Player::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    paused_ = false;
    droppedVideoFramesCount_ = 0;

    // 先中断所有队列阻塞
    if (audioPacketQueue_) {
        audioPacketQueue_->abort();
        audioPacketQueue_->clear();
    }
    if (videoPacketQueue_) {
        videoPacketQueue_->abort();
        videoPacketQueue_->clear();
    }
    if (audioFrameQueue_) {
        audioFrameQueue_->abort();
        audioFrameQueue_->clear();
    }
    if (videoFrameQueue_) {
        videoFrameQueue_->abort();
        videoFrameQueue_->clear();
    }

    // 停止解码器，解码器中如果阻塞了pop，也会因队列abort返回
    if (audioDecoder_) {
        audioDecoder_->stop();
    }
    if (videoDecoder_) {
        videoDecoder_->stop();
    }

    // 停止音频输出，结束音频渲染线程
    if (audioOutput_) {
        audioOutput_->stop();
    }
    if (audioOutputThread_.joinable()) {
        audioOutputThread_.join();
    }

    // 停止视频输出，结束视频渲染线程
    if (videoOutput_) {
        videoOutput_->stop();
    }
    if (videoOutputThread_.joinable()) {
        videoOutputThread_.join();
    }

    // 停止解复用线程
    if (demuxer_) {
        demuxer_->stop();
    }

    // 恢复队列可用状态，以备重新open时使用
    if (audioPacketQueue_) {
        audioPacketQueue_->start();
    }
    if (videoPacketQueue_) {
        videoPacketQueue_->start();
    }
    if (audioFrameQueue_) {
        audioFrameQueue_->start();
    }
    if (videoFrameQueue_) {
        videoFrameQueue_->start();
    }
}

void Player::notifyProgressChanged() {
    if (callback_) {
        callback_->onProgress(syncManager_->getClockTime(), mediaInfo_.durationMs_);
    }
}

void Player::syncClockIfNeeded(int64_t pts) {
    if (requiresSyncClock_.load()) {
        //        syncManager_->updateTime(pts / 1000.0);
        requiresSyncClock_ = false;
    }
}

void Player::pause() {
    if (!running_ || paused_) {
        return;  // 已经暂停或未运行
    }
    paused_ = true;

    // 暂停外部时钟
    if (syncManager_) {
        syncManager_->pause();
    }

    // 停止解复用和解码线程继续读取数据
    demuxer_->pause();
    if (audioDecoder_) {
        audioDecoder_->pause();
        audioPacketQueue_->abort();
        audioFrameQueue_->abort();
    }
    if (videoDecoder_) {
        videoDecoder_->pause();
        videoPacketQueue_->abort();
        videoFrameQueue_->abort();
    }

    // 暂停音频输出
    audioOutput_->pause();
}

void Player::resume() {
    if (!running_ || !paused_) {
        return;  // 未暂停或未运行
    }
    paused_ = false;

    if (syncManager_) {
        syncManager_->resume();
    }

    demuxer_->resume();
    if (audioDecoder_) {
        audioPacketQueue_->start();
        audioFrameQueue_->start();
        audioDecoder_->resume();
    }
    if (videoDecoder_) {
        videoPacketQueue_->start();
        videoFrameQueue_->start();
        videoDecoder_->resume();
    }

    // 恢复音频输出
    audioOutput_->resume();
}

bool Player::seek(int64_t positionMs) {
    if (!running_) return false;

    pause();  // 暂停所有模块，避免数据冲突

    // 设置所有队列进入 abort 状态以终止阻塞
    audioPacketQueue_->abort();
    videoPacketQueue_->abort();
    audioFrameQueue_->abort();
    videoFrameQueue_->abort();

    // 清空队列数据
    audioPacketQueue_->clear();
    videoPacketQueue_->clear();
    audioFrameQueue_->clear();
    videoFrameQueue_->clear();

    // 通知解复用器跳转
    if (!demuxer_->seek(positionMs)) {
        resume();  // 恢复播放状态
        return false;
    }

    // 刷新解码器内部状态（丢弃之前缓冲的帧）
    if (audioDecoder_) audioDecoder_->flush();
    if (videoDecoder_) videoDecoder_->flush();

    // 音视频输出模块同步清理（如果有缓存）
    audioProcessor_->flush();
    audioOutput_->flush();
    //    videoOutput_->flush();

    // 解除 abort 状态，恢复继续解码和播放
    audioPacketQueue_->start();
    videoPacketQueue_->start();
    audioFrameQueue_->start();
    videoFrameQueue_->start();

    resume();  // 恢复播放
    return true;
}

void Player::audioOutputThread() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.output.audio");
#endif

    if (audioProcessor_ && mediaInfo_.hasAudio_) {
        audioProcessor_->setPlaybackRate(playbackRate_.load());
    }

    audioPlaybackRateDelt_ = 0;
    audioOutput_->setPlaybackCallback([this](int64_t pts, int64_t duration) {
        int64_t adjustedDuration = duration / (playbackRate_ + audioPlaybackRateDelt_);
        syncManager_->updateAudioTime(pts / 1000.0, adjustedDuration / 1000.0);
        double delay = syncManager_->computeAudioTargetDelay((pts + adjustedDuration) / 1000.0);
        LogInfo << "audio delay: " << delay;

        double absDelay = fabs(delay);
        double sign = (delay > 0) ? 1.0 : -1.0;

        if (absDelay < 0.010) {
            audioPlaybackRateDelt_ = 0.0;
        } else if (absDelay < 0.030) {
            audioPlaybackRateDelt_ = 0.03 * sign;
        } else if (absDelay < 0.050) {
            audioPlaybackRateDelt_ = 0.05 * sign;
        } else {
            audioPlaybackRateDelt_ = 0.2 * sign;
        }

        if (audioProcessor_) {
            audioProcessor_->setPlaybackRate(playbackRate_ + audioPlaybackRateDelt_);
        }
        notifyProgressChanged();
    });

    audioOutput_->start();

    while (running_) {
        if (paused_) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }
        auto frameHandle = audioFrameQueue_->pop();
        if (frameHandle) {
            auto audioFrame = audioProcessor_->processAudioFrame(frameHandle, 0);
            if (audioFrame) {
                audioOutput_->enqueueAudioFrame(*audioFrame);
            } else {
                av_usleep(static_cast<unsigned int>(10 * 1000));
            }
        } else {
            av_usleep(static_cast<unsigned int>(10 * 1000));
        }
    }
    audioOutput_->stop();
}

void Player::videoOutputThread() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.output.video");
#endif

    while (running_) {
        if (frameTimer_ <= 0.0) {
            frameTimer_ = av_gettime_relative() / 1000000.0;  // 获取当前时间戳（秒）
        }
        if (paused_) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }
        auto frameHandle = videoFrameQueue_->pop();
        if (!frameHandle) {
            av_usleep(static_cast<unsigned int>(10 * 1000));
            continue;
        }
        auto videoFrame = videoProcessor_->processAudioFrame(frameHandle);

        if (videoFrame) {
            int64_t pts = videoFrame->pts_;
            if (std::isnan(lastVideoPts_)) {
                lastVideoPts_ = pts;
                syncManager_->updateVideoTime(pts / 1000.0);
            }
            double lastDuration = pts - lastVideoPts_;
            double delay = syncManager_->computeVideoTargetDelay(lastDuration / 1000.0);
            double time = av_gettime_relative() / 1000000.0;
            double sleepTime = delay;
            LogInfo << "last duration: " << lastDuration << ", sleep time: " << sleepTime;
            if (time < frameTimer_ + delay) {
                sleepTime = FFMIN(frameTimer_ + delay - time, sleepTime);
                LogInfo << "final sleep time" << sleepTime;
                av_usleep(static_cast<unsigned int>(sleepTime * 1000 * 1000));
            }
            videoOutput_->renderVideoFrame(*videoFrame);
            frameTimer_ += delay;
            if (delay > 0 && time - frameTimer_ > AV_SYNC_THRESHOLD_MAX) frameTimer_ = time;
            lastVideoPts_ = pts;
            syncManager_->updateVideoTime(pts / 1000.0);
        }
    }
}

void Player::setPlaybackRate(float rate) {
    if (rate <= 0.0f) return;
    if (rate == playbackRate_) return;

    rate = std::max(0.25f, std::min(4.0f, rate));
    syncManager_->setSpeed(rate);

    if (audioProcessor_) {
        audioProcessor_->flush();
        audioProcessor_->setPlaybackRate(rate);
    }
    playbackRate_.store(rate);
    audioOutput_->flush();
}

float Player::getPlaybackRate() const { return playbackRate_.load(); }

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
