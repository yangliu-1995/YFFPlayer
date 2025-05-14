#include "Player.h"

#include <chrono>
#include <thread>

extern "C" {
#include <libavutil/time.h>
}

namespace yffplayer {

// 缓冲区大小常量
constexpr int PACKET_BUFFER_SIZE = 100;
constexpr int FRAME_BUFFER_SIZE = 30;

// 同步阈值常量（微秒）
constexpr int64_t SYNC_THRESHOLD_US = 5000;  // 5毫秒

Player::Player(std::shared_ptr<PlayerCallback> callback,
               std::shared_ptr<AudioRenderer> audioRenderer,
               std::shared_ptr<VideoRenderer> videoRenderer,
               std::shared_ptr<Logger> logger)
    : mCallback(callback),
      mAudioRenderer(audioRenderer),
      mVideoRenderer(videoRenderer),
      mLogger(logger) {
    // 初始化缓冲区
    mAudioPacketBuffer =
        std::make_shared<BufferQueue<AVPacket *>>(PACKET_BUFFER_SIZE);
    mVideoPacketBuffer =
        std::make_shared<BufferQueue<AVPacket *>>(PACKET_BUFFER_SIZE);
    mAudioFrameBuffer =
        std::make_shared<BufferQueue<std::shared_ptr<AudioFrame>>>(
            FRAME_BUFFER_SIZE);
    mVideoFrameBuffer =
        std::make_shared<BufferQueue<std::shared_ptr<VideoFrame>>>(
            FRAME_BUFFER_SIZE);

    mLogger->log(LogLevel::Info, "Player", "播放器初始化完成");
}

Player::~Player() {
    close();
    mLogger->log(LogLevel::Info, "Player", "播放器已销毁");
}

bool Player::open(const std::string &url) {
    //    std::lock_guard<std::mutex> lock(mStateMutex);

    if (mState != PlayerState::IDLE && mState != PlayerState::STOPPED) {
        mLogger->log(LogLevel::Error, "Player", "播放器状态错误，无法打开媒体");
        return false;
    }

    updateState(PlayerState::INITIALIZED);

    // 创建解复用器
    mDemuxer = std::make_shared<Demuxer>(mAudioPacketBuffer, mVideoPacketBuffer,
                                         mLogger);

    // 打开媒体文件
    if (!mDemuxer->open(url)) {
        mLogger->log(LogLevel::Error, "Player", "打开媒体文件失败: " + url);
        updateState(PlayerState::ERROR);
        return false;
    }

    // 获取媒体信息
    mMediaInfo = mDemuxer->getMediaInfo();

    // 通知回调
    if (mCallback) {
        mCallback->onMediaInfo(mMediaInfo);
    }

    // 创建解码器
    if (mMediaInfo.hasAudio) {
        mAudioDecoder = std::make_shared<AudioDecoder>(
            mAudioPacketBuffer, mAudioFrameBuffer, mLogger);
        if (!mAudioDecoder->open(mMediaInfo.audioCodecParam)) {
            mLogger->log(LogLevel::Error, "Player", "初始化音频解码器失败");
            updateState(PlayerState::ERROR);
            return false;
        }
    }

    if (mMediaInfo.hasVideo) {
        mVideoDecoder = std::make_shared<VideoDecoder>(
            mVideoPacketBuffer, mVideoFrameBuffer, mLogger);
        if (!mVideoDecoder->open(mMediaInfo.videoCodecParam)) {
            mLogger->log(LogLevel::Error, "Player", "初始化视频解码器失败");
            updateState(PlayerState::ERROR);
            return false;
        }
    }

    // 初始化渲染器
    if (mMediaInfo.hasAudio && mAudioRenderer) {
        bool ret = mAudioRenderer->init(
            kAudioTargetSampleRate, kAudioTargetChannels, kAudioTargetBitDepth,
            this->shared_from_this());
        if (!ret) {
            mLogger->log(LogLevel::Error, "Player", "初始化音频渲染器失败");
            updateState(PlayerState::ERROR);
            return false;
        }
    }

    if (mMediaInfo.hasVideo && mVideoRenderer) {
        if (!mVideoRenderer->init(mMediaInfo.videoWidth, mMediaInfo.videoHeight,
                                  PixelFormat::YUV420P,  // 假设默认使用YUV420P
                                  this->shared_from_this())) {
            mLogger->log(LogLevel::Error, "Player", "初始化视频渲染器失败");
            updateState(PlayerState::ERROR);
            return false;
        }
    }

    updateState(PlayerState::PREPARED);
    mLogger->log(LogLevel::Info, "Player", "媒体准备完成: " + url);
    return true;
}

bool Player::start() {
    std::lock_guard<std::mutex> lock(mStateMutex);

    if (mState != PlayerState::PREPARED && mState != PlayerState::PAUSED &&
        mState != PlayerState::COMPLETED) {
        mLogger->log(LogLevel::Error, "Player", "播放器状态错误，无法开始播放");
        return false;
    }

    // 启动解复用器
    mDemuxer->start();

    // 启动解码器
    if (mMediaInfo.hasAudio && mAudioDecoder) {
        mAudioDecoder->start();
    }

    if (mMediaInfo.hasVideo && mVideoDecoder) {
        mVideoDecoder->start();
    }

    // 重置时钟
    mAudioClock = 0;
    mVideoClock = 0;
    mStartTime = getCurrentTimeUs();

    // 启动播放线程
    mIsPlaying = true;
    if (mMediaInfo.hasVideo && mVideoRenderer) {
        mVideoPlayThread = std::thread(&Player::videoPlayLoop, this);
    }

    // 如果有音频，开始播放第一帧
    constexpr int minFrames = 30;  // 至少等待3帧
    auto startTime = getCurrentTimeUs();
    constexpr int64_t timeoutUs = 1000000;  // 1秒超时
    while (mAudioFrameBuffer->size() < minFrames && mIsPlaying) {
        if (getCurrentTimeUs() - startTime > timeoutUs) {
            mLogger->log(LogLevel::Warning, "Player", "等待音频帧超时");
            break;
        }
        av_usleep(10000);  // 等待10ms
    }
    if (!mAudioFrameBuffer->empty()) {
        playNextAudioFrame();  // 缓冲区有数据后再开始播放
    } else {
        mLogger->log(LogLevel::Error, "Player", "音频缓冲区为空，无法开始播放");
        return false;
    }

    updateState(PlayerState::STARTED);
    mLogger->log(LogLevel::Info, "Player", "开始播放");
    return true;
}

bool Player::pause() {
    std::lock_guard<std::mutex> lock(mStateMutex);

    if (mState != PlayerState::STARTED) {
        mLogger->log(LogLevel::Error, "Player", "播放器状态错误，无法暂停");
        return false;
    }

    // 暂停音频渲染
    if (mMediaInfo.hasAudio && mAudioRenderer) {
        mAudioRenderer->pause();
    }

    // 暂停播放线程
    mIsPlaying = false;

    updateState(PlayerState::PAUSED);
    mLogger->log(LogLevel::Info, "Player", "播放已暂停");
    return true;
}

bool Player::resume() {
    std::lock_guard<std::mutex> lock(mStateMutex);

    if (mState != PlayerState::PAUSED) {
        mLogger->log(LogLevel::Error, "Player", "播放器状态错误，无法恢复播放");
        return false;
    }

    // 恢复音频渲染
    if (mMediaInfo.hasAudio && mAudioRenderer) {
        mAudioRenderer->resume();
    }

    // 恢复播放线程
    mIsPlaying = true;
    if (mMediaInfo.hasVideo && mVideoRenderer && !mVideoPlayThread.joinable()) {
        mVideoPlayThread = std::thread(&Player::videoPlayLoop, this);
    }

    updateState(PlayerState::STARTED);
    mLogger->log(LogLevel::Info, "Player", "播放已恢复");
    return true;
}

bool Player::stop() {
    std::lock_guard<std::mutex> lock(mStateMutex);

    if (mState == PlayerState::IDLE || mState == PlayerState::STOPPED) {
        return true;
    }

    // 停止播放线程
    mIsPlaying = false;
    if (mVideoPlayThread.joinable()) {
        mVideoPlayThread.join();
    }

    // 停止解码器
    if (mAudioDecoder) {
        mAudioDecoder->stop();
    }

    if (mVideoDecoder) {
        mVideoDecoder->stop();
    }

    // 停止解复用器
    if (mDemuxer) {
        mDemuxer->stop();
    }

    // 停止渲染器
    if (mAudioRenderer) {
        mAudioRenderer->stop();
    }

    updateState(PlayerState::STOPPED);
    mLogger->log(LogLevel::Info, "Player", "播放已停止");
    return true;
}

bool Player::close() {
    stop();

    std::lock_guard<std::mutex> lock(mStateMutex);

    // 关闭解码器
    if (mAudioDecoder) {
        mAudioDecoder->close();
        mAudioDecoder = nullptr;
    }

    if (mVideoDecoder) {
        mVideoDecoder->close();
        mVideoDecoder = nullptr;
    }

    // 关闭解复用器
    if (mDemuxer) {
        mDemuxer = nullptr;
    }

    // 释放渲染器资源
    if (mAudioRenderer) {
        mAudioRenderer->release();
    }

    if (mVideoRenderer) {
        mVideoRenderer->release();
    }

    // 清空缓冲区

    mAudioFrameBuffer->clear();
    mVideoFrameBuffer->clear();

    updateState(PlayerState::IDLE);
    mLogger->log(LogLevel::Info, "Player", "播放器已关闭");
    return true;
}

bool Player::seek(int64_t position) {
    std::lock_guard<std::mutex> lock(mStateMutex);

    if (mState != PlayerState::STARTED && mState != PlayerState::PAUSED &&
        mState != PlayerState::COMPLETED) {
        mLogger->log(LogLevel::Error, "Player", "播放器状态错误，无法跳转");
        return false;
    }

    // 暂停播放
    bool wasPlaying = (mState == PlayerState::STARTED);
    if (wasPlaying) {
        pause();
    }

    // 清空缓冲区
    clearPacketBuffer();
    mAudioFrameBuffer->clear();
    mVideoFrameBuffer->clear();

    // 执行跳转
    if (mDemuxer) {
        mDemuxer->seek(position);
    }

    // 重置时钟
    mAudioClock = position;
    mVideoClock = position;
    mStartTime = getCurrentTimeUs() - position;

    // 如果之前在播放，恢复播放
    if (wasPlaying) {
        resume();
    }

    mLogger->log(LogLevel::Info, "Player",
                 "跳转到: " + std::to_string(position) + " 微秒");
    return true;
}

PlayerState Player::getState() const { return mState; }

int64_t Player::getCurrentPosition() const {
    // 优先使用音频时钟，如果没有音频则使用视频时钟
    if (mMediaInfo.hasAudio) {
        return mAudioClock;
    } else if (mMediaInfo.hasVideo) {
        return mVideoClock;
    }
    return 0;
}

int64_t Player::getDuration() const {
    return mMediaInfo.durationMs * 1000;  // 转换为微秒
}

void Player::setVolume(float volume) {
    if (mAudioRenderer) {
        mAudioRenderer->setVolume(volume);
    }
}

float Player::getVolume() const {
    if (mAudioRenderer) {
        return mAudioRenderer->getVolume();
    }
    return 0.0f;
}

void Player::setPlaybackRate(float rate) {
    mPlaybackRate = rate;
    if (mDemuxer) {
        mDemuxer->setPlaybackRate(rate);
    }
}

float Player::getPlaybackRate() const { return mPlaybackRate; }

void Player::setMute(bool mute) {
    if (mAudioRenderer) {
        mAudioRenderer->setMute(mute);
    }
}

bool Player::isMuted() const {
    if (mAudioRenderer) {
        return mAudioRenderer->isMuted();
    }
    return false;
}

void Player::onAudioFrameRendered(const AudioFrame &frame) {
    // 更新音频时钟
    mAudioClock = frame.pts + frame.duration;

    // 通知进度回调
    if (mCallback) {
        mCallback->onPlaybackProgress(getCurrentPosition() / 1000000.0,
                                      mMediaInfo.durationMs / 1000.0);
    }

    // 播放下一帧音频
    playNextAudioFrame();
}

void Player::onVideoFrameRendered(const VideoFrame &frame) {
    // 更新视频时钟
    mVideoClock = frame.pts + frame.duration;

    // 如果没有音频，通过视频帧通知进度
    if (!mMediaInfo.hasAudio && mCallback) {
        mCallback->onPlaybackProgress(getCurrentPosition() / 1000000.0,
                                      mMediaInfo.durationMs / 1000.0);
    }
}

void Player::videoPlayLoop() {
    mLogger->log(LogLevel::Info, "Player", "视频播放线程已启动");

    while (mIsPlaying) {
        try {
            // 从缓冲区获取视频帧
            std::shared_ptr<VideoFrame> frame;
            if (!mVideoFrameBuffer->tryPop(frame)) {
                // 如果缓冲区为空，睡眠一段时间后继续
                av_usleep(10000);  // 10毫秒 = 10000微秒
                continue;
            }

            // 计算音视频同步延迟
            int64_t delay = calculateSyncDelay(frame->pts);

            // 如果需要等待以保持同步，则睡眠
            if (delay > 0) {
                av_usleep(delay);
            } else if (delay < -SYNC_THRESHOLD_US * 2) {
                // 如果视频落后太多，跳过这一帧
                //                mLogger->log(
                //                    LogLevel::Verbose, "Player",
                //                    "视频帧丢弃，延迟: " +
                //                    std::to_string(delay) + " 微秒");
                continue;
            }

            // 渲染视频帧
            if (mVideoRenderer) {
                if (!mVideoRenderer->render(*frame)) {
                    mLogger->log(LogLevel::Error, "Player", "渲染视频帧失败");
                }
            }

            // 检查是否到达文件末尾
            if (frame->pts >= mMediaInfo.durationMs * 1000 &&
                !mDemuxer->isLive()) {
                // 如果是最后一帧，并且没有更多帧，则播放完成
                if (mVideoFrameBuffer->empty()) {
                    updateState(PlayerState::COMPLETED);
                    mIsPlaying = false;
                    mLogger->log(LogLevel::Info, "Player", "播放完成");

                    // 通知回调
                    if (mCallback) {
                        mCallback->onPlaybackProgress(1.0, 1.0);  // 100%
                    }
                }
            }
        } catch (const std::exception &e) {
            mLogger->log(LogLevel::Error, "Player",
                         std::string("视频播放线程异常: ") + e.what());
            av_usleep(10000);  // 发生异常时等待一段时间
        }
    }

    mLogger->log(LogLevel::Info, "Player", "视频播放线程已退出");
}

int64_t Player::calculateSyncDelay(int64_t videoPts) {
    // 如果没有音频，则使用系统时钟同步
    if (!mMediaInfo.hasAudio) {
        int64_t elapsedTime = getCurrentTimeUs() - mStartTime;
        return videoPts - elapsedTime;
    }

    // 使用音频时钟同步
    int64_t diff = videoPts - mAudioClock;

    // 如果视频帧比音频早，需要等待
    if (diff > 0) {
        // 但如果差距太大，可能是时钟异常，限制最大等待时间
        return std::min(diff, (int64_t)100000);  // 最多等待100毫秒
    }

    // 如果视频帧比音频晚，返回负值，可能需要丢弃帧
    return diff;
}

int64_t Player::getCurrentTimeUs() {
    // 使用FFmpeg的时间函数获取当前时间（微秒）
    return av_gettime();
}

bool Player::playNextAudioFrame() {
    if (!mAudioRenderer || !mMediaInfo.hasAudio) {
        return false;
    }

    // 从缓冲区获取音频帧
    std::shared_ptr<AudioFrame> frame;
    if (!mAudioFrameBuffer->tryPop(frame)) {
        // 如果缓冲区为空，返回失败
        return false;
    }

    // 渲染音频帧
    if (!mAudioRenderer->play(*frame)) {
        mLogger->log(LogLevel::Error, "Player", "渲染音频帧失败");
        return false;
    }

    // 检查是否到达文件末尾
    if (frame->pts >= mMediaInfo.durationMs * 1000 && !mDemuxer->isLive()) {
        // 如果是最后一帧，并且没有更多帧，则播放完成
        if (mAudioFrameBuffer->empty() && !mMediaInfo.hasVideo) {
            updateState(PlayerState::COMPLETED);
            mIsPlaying = false;
            mLogger->log(LogLevel::Info, "Player", "播放完成");

            // 通知回调
            if (mCallback) {
                mCallback->onPlaybackProgress(1.0, 1.0);  // 100%
            }
        }
    }

    return true;
}

void Player::clearPacketBuffer() {
    while (!mAudioPacketBuffer->empty()) {
        auto packet = mAudioPacketBuffer->pop();
        if (packet) {
            av_packet_free(&packet);
        }
    }
    while (!mVideoPacketBuffer->empty()) {
        auto packet = mVideoPacketBuffer->pop();
        if (packet) {
            av_packet_free(&packet);
        }
    }
}

void Player::updateState(PlayerState state) {
    PlayerState oldState = mState;
    mState = state;

    // 如果状态发生变化，通知回调
    if (oldState != state && mCallback) {
        mCallback->onPlayerStateChanged(state);
    }

    mLogger->log(
        LogLevel::Verbose, "Player",
        "播放器状态变更: " + std::to_string(static_cast<int>(oldState)) +
            " -> " + std::to_string(static_cast<int>(state)));
}

void Player::onDemuxerStateChanged(DemuxerState state) {}

void Player::onDemuxerError(const Error &error) {}

void Player::onEndOfFile() {}

void Player::onMediaInfoReady(const MediaInfo &info) {}

void Player::onSeekCompleted(int64_t position) {}

}  // namespace yffplayer
