#include "Demuxer.h"

#include "Log.h"
#include "MediaInfo.h"
#include "Packet.h"

#if defined(__APPLE__)
#include <pthread.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace yffplayer {

Demuxer::Demuxer(std::shared_ptr<PacketQueue> audioQueue, std::shared_ptr<PacketQueue> videoQueue)
    : audioQueue_(audioQueue), videoQueue_(videoQueue) {}

Demuxer::~Demuxer() {
    stop();
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
    }
}

bool Demuxer::open(const std::string& url, MediaInfo& mediaInfo) {
    AVDictionary* options = nullptr;

    av_dict_set(&options, "fflags", "nobuffer", 0);

    av_dict_set(&options, "probesize", "32", 0);
    av_dict_set(&options, "analyzeduration", "100000", 0);
    av_dict_set(&options, "rtmp_buffer", "100", 0);
    av_dict_set(&options, "rtmp_live", "live", 0);
    av_dict_set(&options, "flush_packets", "1", 0);
    av_dict_set(&options, "timeout", "3000000", 0);

    int ret = avformat_open_input(&formatCtx_, url.c_str(), nullptr, nullptr);
    if (ret < 0) {
        LogInfo << "Failed to open input: " << url << "ret: " << ret << "\n";
        if (auto callback = callback_.lock()) {
            Error error(ErrorCode::FILE_OPEN_FAILED, "Failed to open input file: " + url +
                                                         ", error code: " + std::to_string(ret));
            callback->onReadError(error);
        }
        return false;
    }

    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0) {
        LogInfo << "Failed to find stream info\n";
        if (auto callback = callback_.lock()) {
            Error error(ErrorCode::STREAM_INFO_FAILED,
                        "Failed to find stream info, error code: " + std::to_string(ret));
            callback->onReadError(error);
        }
        return false;
    }

    mediaInfo.isLiveStream_ = false;
    if (formatCtx_->duration == AV_NOPTS_VALUE || formatCtx_->duration <= 0) {
        mediaInfo.isLiveStream_ = true;
    }

    if (formatCtx_->iformat && formatCtx_->iformat->name) {
        std::string formatName = formatCtx_->iformat->name;
        if (formatName.find("rtmp") != std::string::npos ||
            formatName.find("rtsp") != std::string::npos ||
            formatName.find("hls") != std::string::npos ||
            formatName.find("dash") != std::string::npos) {
            mediaInfo.isLiveStream_ = true;
        }
    }

    if (url.find("rtmp://") == 0 || url.find("rtsp://") == 0 || url.find("http://") == 0 ||
        url.find("https://") == 0) {
        // 对于网络流，进一步检查是否为直播
        if (formatCtx_->duration == AV_NOPTS_VALUE) {
            mediaInfo.isLiveStream_ = true;
        }
    }

    if (formatCtx_->duration != AV_NOPTS_VALUE) {
        mediaInfo.durationMs_ = formatCtx_->duration / (AV_TIME_BASE / 1000);
    }

    audioStreamIndex_ = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    videoStreamIndex_ = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    if (audioStreamIndex_ >= 0) {
        mediaInfo.hasAudio_ = true;
        auto codecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(codecParams, formatCtx_->streams[audioStreamIndex_]->codecpar);
        mediaInfo.audioCodecParameters_ = codecParams;
        mediaInfo.audioChannels_ = mediaInfo.audioCodecParameters_->ch_layout.nb_channels;
        mediaInfo.audioSampleRate_ = mediaInfo.audioCodecParameters_->sample_rate;
        mediaInfo.audioTimeBase_ = formatCtx_->streams[audioStreamIndex_]->time_base;
    }
    if (videoStreamIndex_ >= 0) {
        mediaInfo.hasVideo_ = true;
        auto codecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(codecParams, formatCtx_->streams[videoStreamIndex_]->codecpar);
        mediaInfo.videoCodecParameters_ = codecParams;
        mediaInfo.videoWidth_ = mediaInfo.videoCodecParameters_->width;
        mediaInfo.videoHeight_ = mediaInfo.videoCodecParameters_->height;
        mediaInfo.videoTimeBase_ = formatCtx_->streams[videoStreamIndex_]->time_base;
        AVRational frameRate = formatCtx_->streams[videoStreamIndex_]->avg_frame_rate;
        if (frameRate.num == 0 || frameRate.den == 0) {
            frameRate = formatCtx_->streams[videoStreamIndex_]->r_frame_rate;
        }
        mediaInfo.videoFrameRate_ =
            frameRate.num && frameRate.den ? static_cast<int>(av_q2d(frameRate) + 0.5) : 0;
    }

    mediaInfo.isTsDiscont_ = formatCtx_->flags & AVFMT_TS_DISCONT;

    bool hasStreams = audioStreamIndex_ != -1 || videoStreamIndex_ != -1;
    if (!hasStreams) {
        if (auto callback = callback_.lock()) {
            Error error(ErrorCode::NO_STREAMS_FOUND,
                        "No audio or video streams found in the media file");
            callback->onReadError(error);
        }
    }

    return hasStreams;
}

void Demuxer::start() {
    if (running_) return;
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread(&Demuxer::demuxLoop, this);

    if (auto callback = callback_.lock()) {
        callback->onDemuxStarted();
    }
}

void Demuxer::pause() {
    paused_ = true;
    if (auto callback = callback_.lock()) {
        callback->onDemuxPaused();
    }
}

void Demuxer::resume() {
    paused_ = false;
    cond_.notify_all();
    if (auto callback = callback_.lock()) {
        callback->onDemuxResumed();
    }
}

bool Demuxer::seek(int64_t timestampMs) {
    if (auto callback = callback_.lock()) {
        callback->onSeekStarted(timestampMs);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    seeking_ = true;

    int64_t seekTarget = static_cast<int64_t>(timestampMs * AV_TIME_BASE / 1000);
    int ret = avformat_seek_file(formatCtx_, -1, INT64_MIN, seekTarget, INT64_MAX, 0);
    if (ret < 0) {
        LogInfo << "av_seek_frame failed: " << ret;
        if (auto callback = callback_.lock()) {
            Error error(ErrorCode::SEEK_FAILED,
                        "Seek operation failed, error code: " + std::to_string(ret));
            callback->onSeekFailed(timestampMs, error);
        }
        seeking_ = false;
        return false;
    }
    avformat_flush(formatCtx_);

    // 清空队列避免旧数据
    audioQueue_->clear();
    videoQueue_->clear();

    seeking_ = false;

    if (auto callback = callback_.lock()) {
        callback->onSeekCompleted(timestampMs);
    }
    return true;
}

void Demuxer::stop() {
    if (!running_) return;
    stopRequested_ = true;
    resume();  // 防止阻塞在 paused
    if (thread_.joinable()) {
        thread_.join();
    }
    if (formatCtx_) {
        avformat_flush(formatCtx_);
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }
    running_ = false;

    if (auto callback = callback_.lock()) {
        callback->onDemuxStopped();
    }
}

void Demuxer::setCallback(std::shared_ptr<DemuxerCallback> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = callback;
}

void Demuxer::demuxLoop() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.demuxer");
#endif
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        if (auto callback = callback_.lock()) {
            Error error(ErrorCode::PACKET_ALLOCATION_FAILED, "Failed to allocate AVPacket");
            callback->onReadError(error);
        }
        return;
    }

    int64_t lastProgressTime = 0;
    const int64_t progressInterval = 1000;  // 每秒报告一次进度

    while (!stopRequested_) {
        // 暂停逻辑
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !paused_ || stopRequested_; });
        lock.unlock();

        if (stopRequested_ || seeking_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int ret = av_read_frame(formatCtx_, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LogInfo << "End of file\n";
                if (auto callback = callback_.lock()) {
                    callback->onEndOfFile();
                }
                break;
            } else if (ret == AVERROR(ETIMEDOUT)) {
                if (auto callback = callback_.lock()) {
                    Error error(ErrorCode::NETWORK_TIMEOUT, "Network timeout while reading frame");
                    callback->onNetworkError(error);
                }
            } else {
                if (auto callback = callback_.lock()) {
                    Error error(ErrorCode::READ_FRAME_FAILED,
                                "Failed to read frame, error code: " + std::to_string(ret));
                    callback->onReadError(error);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 报告进度
        if (auto callback = callback_.lock()) {
            if (pkt->pts != AV_NOPTS_VALUE) {
                int64_t currentTime = 0;
                if (pkt->stream_index == videoStreamIndex_ && videoStreamIndex_ >= 0) {
                    currentTime =
                        av_rescale_q(pkt->pts, formatCtx_->streams[videoStreamIndex_]->time_base,
                                     AV_TIME_BASE_Q) /
                        1000;
                } else if (pkt->stream_index == audioStreamIndex_ && audioStreamIndex_ >= 0) {
                    currentTime =
                        av_rescale_q(pkt->pts, formatCtx_->streams[audioStreamIndex_]->time_base,
                                     AV_TIME_BASE_Q) /
                        1000;
                }

                if (currentTime - lastProgressTime >= progressInterval) {
                    int64_t duration = formatCtx_->duration != AV_NOPTS_VALUE
                                           ? formatCtx_->duration / (AV_TIME_BASE / 1000)
                                           : 0;
                    callback->onDemuxProgress(currentTime, duration);
                    lastProgressTime = currentTime;
                }
            }
        }

        if (pkt->stream_index == videoStreamIndex_) {
            auto packet = std::make_shared<Packet>(av_packet_clone(pkt));
            videoQueue_->push(packet);
        } else if (pkt->stream_index == audioStreamIndex_) {
            auto packet = std::make_shared<Packet>(av_packet_clone(pkt));
            audioQueue_->push(packet);
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}

}  // namespace yffplayer
