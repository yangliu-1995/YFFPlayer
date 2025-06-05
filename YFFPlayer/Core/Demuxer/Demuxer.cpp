#include "Demuxer.h"

#include <iostream>

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
    : mAudioQueue(audioQueue), mVideoQueue(videoQueue) {}

Demuxer::~Demuxer() {
    stop();
    if (mFormatCtx) {
        avformat_close_input(&mFormatCtx);
    }
}

bool Demuxer::open(const std::string& url, MediaInfo& mediaInfo) {
    AVDictionary* options = nullptr;

    // 尽快打开流，尽量不要缓冲太久
    av_dict_set(&options, "fflags", "nobuffer", 0);

    // 设定最大探测时间（单位微秒，1000000 = 1 秒），越小启动越快（但不一定稳定）
    av_dict_set(&options, "probesize", "32", 0);            // 默认 5MB，可设为更小
    av_dict_set(&options, "analyzeduration", "100000", 0);  // 默认 5 秒，这里设为 0.1 秒

    // 如果是 RTMP 流，建议加上：
    av_dict_set(&options, "rtmp_buffer", "100", 0);  // 缓冲时间，单位 ms
    av_dict_set(&options, "rtmp_live", "live", 0);   // 告诉服务器这是直播

    // 更快地丢包处理（实时流更重要）
    av_dict_set(&options, "flush_packets", "1", 0);

    // 如果是 TCP 协议（如 HTTP-FLV），可以设置连接超时（单位微秒）
    av_dict_set(&options, "timeout", "3000000", 0);  // 3 秒

    int ret = avformat_open_input(&mFormatCtx, url.c_str(), nullptr, nullptr);
    if (ret < 0) {
        LogInfo << "Failed to open input: " << url << "ret: " << ret << "\n";
        if (auto callback = mCallback.lock()) {
            Error error(ErrorCode::FILE_OPEN_FAILED, "Failed to open input file: " + url +
                                                         ", error code: " + std::to_string(ret));
            callback->onReadError(error);
        }
        return false;
    }

    ret = avformat_find_stream_info(mFormatCtx, nullptr);
    if (ret < 0) {
        LogInfo << "Failed to find stream info\n";
        if (auto callback = mCallback.lock()) {
            Error error(ErrorCode::STREAM_INFO_FAILED,
                        "Failed to find stream info, error code: " + std::to_string(ret));
            callback->onReadError(error);
        }
        return false;
    }

    mediaInfo.mIsLiveStream = false;
    if (mFormatCtx->duration == AV_NOPTS_VALUE || mFormatCtx->duration <= 0) {
        mediaInfo.mIsLiveStream = true;
    }

    if (mFormatCtx->iformat && mFormatCtx->iformat->name) {
        std::string formatName = mFormatCtx->iformat->name;
        if (formatName.find("rtmp") != std::string::npos ||
            formatName.find("rtsp") != std::string::npos ||
            formatName.find("hls") != std::string::npos ||
            formatName.find("dash") != std::string::npos) {
            mediaInfo.mIsLiveStream = true;
        }
    }

    if (url.find("rtmp://") == 0 || url.find("rtsp://") == 0 || url.find("http://") == 0 ||
        url.find("https://") == 0) {
        // 对于网络流，进一步检查是否为直播
        if (mFormatCtx->duration == AV_NOPTS_VALUE) {
            mediaInfo.mIsLiveStream = true;
        }
    }

    if (mFormatCtx->duration != AV_NOPTS_VALUE) {
        mediaInfo.mDurationMs = mFormatCtx->duration / (AV_TIME_BASE / 1000);
    }

    mAudioStreamIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    mVideoStreamIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    if (mAudioStreamIndex >= 0) {
        mediaInfo.mHasAudio = true;
        auto codecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(codecParams, mFormatCtx->streams[mAudioStreamIndex]->codecpar);
        mediaInfo.mAudioCodecParameters = codecParams;
        mediaInfo.mAudioChannels = mediaInfo.mAudioCodecParameters->ch_layout.nb_channels;
        mediaInfo.mAudioSampleRate = mediaInfo.mAudioCodecParameters->sample_rate;
        mediaInfo.mAudioTimeBase = mFormatCtx->streams[mAudioStreamIndex]->time_base;
    }
    if (mVideoStreamIndex >= 0) {
        mediaInfo.mHasVideo = true;
        auto codecParams = avcodec_parameters_alloc();
        avcodec_parameters_copy(codecParams, mFormatCtx->streams[mVideoStreamIndex]->codecpar);
        mediaInfo.mVideoCodecParameters = codecParams;
        mediaInfo.mVideoWidth = mediaInfo.mVideoCodecParameters->width;
        mediaInfo.mVideoHeight = mediaInfo.mVideoCodecParameters->height;
        mediaInfo.mVideoTimeBase = mFormatCtx->streams[mVideoStreamIndex]->time_base;
        AVRational frameRate = mFormatCtx->streams[mVideoStreamIndex]->avg_frame_rate;
        if (frameRate.num == 0 || frameRate.den == 0) {
            frameRate = mFormatCtx->streams[mVideoStreamIndex]->r_frame_rate;
        }
        mediaInfo.mVideoFrameRate =
            frameRate.num && frameRate.den ? static_cast<int>(av_q2d(frameRate) + 0.5) : 0;
    }

    bool hasStreams = mAudioStreamIndex != -1 || mVideoStreamIndex != -1;
    if (!hasStreams) {
        if (auto callback = mCallback.lock()) {
            Error error(ErrorCode::NO_STREAMS_FOUND,
                        "No audio or video streams found in the media file");
            callback->onReadError(error);
        }
    }

    return hasStreams;
}

void Demuxer::start() {
    if (mRunning) return;
    mStopRequested = false;
    mRunning = true;
    mThread = std::thread(&Demuxer::demuxLoop, this);

    if (auto callback = mCallback.lock()) {
        callback->onDemuxStarted();
    }
}

void Demuxer::pause() {
    mPaused = true;
    if (auto callback = mCallback.lock()) {
        callback->onDemuxPaused();
    }
}

void Demuxer::resume() {
    mPaused = false;
    mCond.notify_all();
    if (auto callback = mCallback.lock()) {
        callback->onDemuxResumed();
    }
}

bool Demuxer::seek(int64_t timestampMs) {
    if (auto callback = mCallback.lock()) {
        callback->onSeekStarted(timestampMs);
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mSeeking = true;

    int64_t seekTarget = static_cast<int64_t>(timestampMs * AV_TIME_BASE / 1000);
    int ret = avformat_seek_file(mFormatCtx, -1, INT64_MIN, seekTarget, INT64_MAX, 0);
    if (ret < 0) {
        LogInfo << "av_seek_frame failed: " << ret << std::endl;
        if (auto callback = mCallback.lock()) {
            Error error(ErrorCode::SEEK_FAILED,
                        "Seek operation failed, error code: " + std::to_string(ret));
            callback->onSeekFailed(timestampMs, error);
        }
        mSeeking = false;
        return false;
    }
    avformat_flush(mFormatCtx);

    // 清空队列避免旧数据
    mAudioQueue->clear();
    mVideoQueue->clear();

    mSeeking = false;

    if (auto callback = mCallback.lock()) {
        callback->onSeekCompleted(timestampMs);
    }
    return true;
}

void Demuxer::stop() {
    if (!mRunning) return;
    mStopRequested = true;
    resume();  // 防止阻塞在 paused
    if (mFormatCtx) {
        avformat_flush(mFormatCtx);
        avformat_close_input(&mFormatCtx);
    }
    if (mThread.joinable()) {
        mThread.join();
    }
    if (mFormatCtx) {
        avformat_free_context(mFormatCtx);
        mFormatCtx = nullptr;
    }
    mRunning = false;

    if (auto callback = mCallback.lock()) {
        callback->onDemuxStopped();
    }
}

void Demuxer::setCallback(std::shared_ptr<DemuxerCallback> callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCallback = callback;
}

void Demuxer::demuxLoop() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.demuxer");
#endif
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        if (auto callback = mCallback.lock()) {
            Error error(ErrorCode::PACKET_ALLOCATION_FAILED, "Failed to allocate AVPacket");
            callback->onReadError(error);
        }
        return;
    }

    int64_t lastProgressTime = 0;
    const int64_t progressInterval = 1000;  // 每秒报告一次进度

    while (!mStopRequested) {
        // 暂停逻辑
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, [this] { return !mPaused || mStopRequested; });
        lock.unlock();

        if (mStopRequested || mSeeking) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int ret = av_read_frame(mFormatCtx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LogInfo << "End of file\n";
                if (auto callback = mCallback.lock()) {
                    callback->onEndOfFile();
                }
                break;
            } else if (ret == AVERROR(ETIMEDOUT)) {
                if (auto callback = mCallback.lock()) {
                    Error error(ErrorCode::NETWORK_TIMEOUT, "Network timeout while reading frame");
                    callback->onNetworkError(error);
                }
            } else {
                if (auto callback = mCallback.lock()) {
                    Error error(ErrorCode::READ_FRAME_FAILED,
                                "Failed to read frame, error code: " + std::to_string(ret));
                    callback->onReadError(error);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 报告进度
        if (auto callback = mCallback.lock()) {
            if (pkt->pts != AV_NOPTS_VALUE) {
                int64_t currentTime = 0;
                if (pkt->stream_index == mVideoStreamIndex && mVideoStreamIndex >= 0) {
                    currentTime =
                        av_rescale_q(pkt->pts, mFormatCtx->streams[mVideoStreamIndex]->time_base,
                                     AV_TIME_BASE_Q) /
                        1000;
                } else if (pkt->stream_index == mAudioStreamIndex && mAudioStreamIndex >= 0) {
                    currentTime =
                        av_rescale_q(pkt->pts, mFormatCtx->streams[mAudioStreamIndex]->time_base,
                                     AV_TIME_BASE_Q) /
                        1000;
                }

                if (currentTime - lastProgressTime >= progressInterval) {
                    int64_t duration = mFormatCtx->duration != AV_NOPTS_VALUE
                                           ? mFormatCtx->duration / (AV_TIME_BASE / 1000)
                                           : 0;
                    callback->onDemuxProgress(currentTime, duration);
                    lastProgressTime = currentTime;
                }
            }
        }

        if (pkt->stream_index == mVideoStreamIndex) {
            auto packet = std::make_shared<Packet>(av_packet_clone(pkt));
            mVideoQueue->push(packet);
        } else if (pkt->stream_index == mAudioStreamIndex) {
            auto packet = std::make_shared<Packet>(av_packet_clone(pkt));
            mAudioQueue->push(packet);
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}

}  // namespace yffplayer
