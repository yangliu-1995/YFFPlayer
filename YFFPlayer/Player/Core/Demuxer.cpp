#include "Demuxer.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

namespace yffplayer {

Demuxer::Demuxer(std::shared_ptr<BufferQueue<AVPacket*>> audioBuffer,
                 std::shared_ptr<BufferQueue<AVPacket*>> videoBuffer,
                 std::shared_ptr<Logger> logger,
                 std::shared_ptr<DemuxerCallback> callback)
    : mAudioBuffer(audioBuffer),
      mVideoBuffer(videoBuffer),
      mLogger(logger),
      mCallback(callback) {
    mLogger->log(LogLevel::Info, "Demuxer", "解复用器已创建");
    updateState(DemuxerState::IDLE);
}

Demuxer::~Demuxer() {
    stop();
    mLogger->log(LogLevel::Info, "Demuxer", "解复用器已销毁");
}

void Demuxer::setCallback(std::shared_ptr<DemuxerCallback> callback) {
    mCallback = callback;
}

void Demuxer::updateState(DemuxerState state) {
    mState = state;
    if (mCallback) {
        mCallback->onDemuxerStateChanged(state);
    }
}

void Demuxer::notifyError(ErrorCode code, const std::string& message) {
    Error error;
    error.code = code;
    error.message = message;

    mLogger->log(LogLevel::Error, "Demuxer", message);

    if (mCallback) {
        mCallback->onDemuxerError(error);
    }

    updateState(DemuxerState::ERROR);
}

bool Demuxer::open(const std::string& url) {
    std::lock_guard<std::mutex> lock(mMutex);

    // 保存URL
    mUrl = url;

    updateState(DemuxerState::INITIALIZED);

    // 打开输入文件
    AVFormatContext* formatContext = nullptr;
    if (avformat_open_input(&formatContext, url.c_str(), nullptr, nullptr) !=
        0) {
        notifyError(ErrorCode::DEMUXER_OPEN_FAILED, "无法打开媒体文件: " + url);
        return false;
    }

    // 查找流信息
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        notifyError(ErrorCode::DEMUXER_FIND_STREAM_FAILED, "无法查找流信息");
        avformat_close_input(&formatContext);
        return false;
    }

    // 检查是否为直播流
    mIsLive = (formatContext->duration == AV_NOPTS_VALUE);

    // 查找音视频流
    int audioStreamIndex = -1;
    int videoStreamIndex = -1;

    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        AVStream* stream = formatContext->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            audioStreamIndex < 0) {
            audioStreamIndex = i;
            AVCodecParameters* codecParams = avcodec_parameters_alloc();
            avcodec_parameters_copy(codecParams, stream->codecpar);
            mMediaInfo.audioCodecParam = codecParams;
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                   videoStreamIndex < 0) {
            videoStreamIndex = i;
            AVCodecParameters* codecParams = avcodec_parameters_alloc();
            avcodec_parameters_copy(codecParams, stream->codecpar);
            mMediaInfo.videoCodecParam = codecParams;
        }
    }

    mMediaInfo.durationMs = formatContext->duration / AV_TIME_BASE;
    mMediaInfo.hasAudio = (audioStreamIndex >= 0);
    mMediaInfo.hasVideo = (videoStreamIndex >= 0);

    if (mMediaInfo.hasAudio) {
        mMediaInfo.audiochannels =
            formatContext->streams[audioStreamIndex]->codecpar->channels;
        mMediaInfo.audioSampleRate =
            formatContext->streams[audioStreamIndex]->codecpar->sample_rate;
    }

    if (mMediaInfo.hasVideo) {
        mMediaInfo.videoWidth =
            formatContext->streams[videoStreamIndex]->codecpar->width;
        mMediaInfo.videoHeight =
            formatContext->streams[videoStreamIndex]->codecpar->height;
    }

    if (mMediaInfo.hasAudio) {
        if (mMediaInfo.hasVideo) {
            mMediaInfo.type = MediaType::AUDIO_VIDEO;
        } else {
            mMediaInfo.type = MediaType::AUDIO;
        }
    } else {
        if (mMediaInfo.hasVideo) {
            mMediaInfo.type = MediaType::VIDEO;
        } else {
            mMediaInfo.type = MediaType::UNKNOWN;
        }
    }

    // 关闭输入文件，实际播放时会重新打开
    avformat_close_input(&formatContext);

    mLogger->log(LogLevel::Info, "Demuxer", "媒体文件打开成功: " + url);
    mLogger->log(LogLevel::Info, "Demuxer",
                 "音频流索引: " + std::to_string(audioStreamIndex) +
                     ", 视频流索引: " + std::to_string(videoStreamIndex));

    // 通知媒体信息已准备好
    if (mCallback) {
        mCallback->onMediaInfoReady(mMediaInfo);
    }

    return true;
}

void Demuxer::start() {
    if (mIsRunning) {
        return;
    }

    mIsRunning = true;
    mReadThread = std::thread(&Demuxer::readLoop, this);
    mLogger->log(LogLevel::Info, "Demuxer", "解复用线程已启动");
    updateState(DemuxerState::RUNNING);
}

void Demuxer::stop() {
    if (!mIsRunning) {
        return;
    }

    mIsRunning = false;
    if (mReadThread.joinable()) {
        mReadThread.join();
    }
    mLogger->log(LogLevel::Info, "Demuxer", "解复用线程已停止");
    updateState(DemuxerState::STOPPED);
}

void Demuxer::seek(int64_t position) {
    mIsSeeking = true;
    mSeekPosition = position;
    updateState(DemuxerState::SEEKING);
    mLogger->log(LogLevel::Info, "Demuxer",
                 "请求跳转到: " + std::to_string(position) + " 微秒");
}

void Demuxer::setPlaybackRate(float rate) {
    mPlaybackRate = rate;
    mLogger->log(LogLevel::Info, "Demuxer",
                 "播放速率设置为: " + std::to_string(rate));
}

bool Demuxer::isLive() const { return mIsLive; }

MediaInfo Demuxer::getMediaInfo() const { return mMediaInfo; }

void Demuxer::readLoop() {
    AVFormatContext* formatContext = nullptr;
    int audioStreamIndex = -1;
    int videoStreamIndex = -1;
    AVPacket* avPacket = nullptr;

    try {
        // 打开输入文件
        if (avformat_open_input(&formatContext, mUrl.c_str(), nullptr,
                                nullptr) != 0) {
            notifyError(ErrorCode::DEMUXER_OPEN_FAILED,
                        "无法打开媒体文件: " + mUrl);
            return;
        }

        // 查找流信息
        if (avformat_find_stream_info(formatContext, nullptr) < 0) {
            notifyError(ErrorCode::DEMUXER_FIND_STREAM_FAILED,
                        "无法查找流信息");
            avformat_close_input(&formatContext);
            return;
        }

        // 查找音视频流
        for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
            AVStream* stream = formatContext->streams[i];
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                audioStreamIndex < 0) {
                audioStreamIndex = i;
            } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                       videoStreamIndex < 0) {
                videoStreamIndex = i;
            }
        }

        // 分配AVPacket
        avPacket = av_packet_alloc();

        // 主循环
        while (mIsRunning) {
            // 处理seek请求
            if (mIsSeeking) {
                int64_t seekTarget = mSeekPosition;

                // 将微秒转换为AVStream时间基
                if (videoStreamIndex >= 0) {
                    AVStream* stream = formatContext->streams[videoStreamIndex];
                    seekTarget = av_rescale_q(seekTarget, AV_TIME_BASE_Q,
                                              stream->time_base);
                    av_seek_frame(formatContext, videoStreamIndex, seekTarget,
                                  AVSEEK_FLAG_BACKWARD);
                } else if (audioStreamIndex >= 0) {
                    AVStream* stream = formatContext->streams[audioStreamIndex];
                    seekTarget = av_rescale_q(seekTarget, AV_TIME_BASE_Q,
                                              stream->time_base);
                    av_seek_frame(formatContext, audioStreamIndex, seekTarget,
                                  AVSEEK_FLAG_BACKWARD);
                }

                mIsSeeking = false;
                mLogger->log(LogLevel::Info, "Demuxer", "跳转完成");

                // 通知跳转完成
                if (mCallback) {
                    mCallback->onSeekCompleted(mSeekPosition);
                }

                updateState(DemuxerState::RUNNING);
                continue;
            }

            // 检查缓冲区是否已满
            if (mAudioBuffer->full() || mVideoBuffer->full()) {
                // 如果缓冲区已满，睡眠一段时间后继续
                //                mLogger->log(LogLevel::Warning, "Demuxer",
                //                                "缓冲区已满，等待数据处理");
                av_usleep(10000);  // 10毫秒 = 10000微秒
                continue;
            }

            // 读取下一个数据包
            int ret = av_read_frame(formatContext, avPacket);
            if (ret < 0) {
                if (ret == AVERROR_EOF ||
                    (formatContext->pb && formatContext->pb->eof_reached)) {
                    // 文件结束
                    mLogger->log(LogLevel::Info, "Demuxer", "文件结束");

                    // 通知文件结束
                    if (mCallback) {
                        mCallback->onEndOfFile();
                    }

                    // 对于非直播流，可以选择循环播放或停止
                    if (!mIsLive) {
                        // 回到文件开头
                        av_seek_frame(formatContext, -1, 0,
                                      AVSEEK_FLAG_BACKWARD);
                        continue;
                    } else {
                        break;
                    }
                } else {
                    // 其他错误
                    notifyError(ErrorCode::DEMUXER_READ_FAILED,
                                "读取帧错误: " + std::to_string(ret));
                    av_usleep(10000);  // 10毫秒 = 10000微秒
                    continue;
                }
            }

            // 处理音视频数据包
            if (avPacket->stream_index == audioStreamIndex ||
                avPacket->stream_index == videoStreamIndex) {
                AVPacket* packet = av_packet_clone(avPacket);

                if (!packet) {
                    continue;
                }
                // 将数据包放入缓冲区
                if (avPacket->stream_index == audioStreamIndex) {
                    mAudioBuffer->tryPush(packet);
                } else if (avPacket->stream_index == videoStreamIndex) {
                    mVideoBuffer->tryPush(packet);
                }

                // 根据播放速率控制读取速度
                if (mPlaybackRate < 2.0f) {
                    av_usleep(static_cast<unsigned int>(
                        10000 / mPlaybackRate));  // 10毫秒 / 播放速率
                }
            }

            // 释放AVPacket
            av_packet_unref(avPacket);
        }
    } catch (const std::exception& e) {
        notifyError(ErrorCode::DEMUXER_EXCEPTION,
                    std::string("解复用循环异常: ") + e.what());
    }

    // 清理资源
    if (avPacket) {
        av_packet_free(&avPacket);
    }

    if (formatContext) {
        avformat_close_input(&formatContext);
    }

    mLogger->log(LogLevel::Info, "Demuxer", "解复用线程已退出");
}

}  // namespace yffplayer
