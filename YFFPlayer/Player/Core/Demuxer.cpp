#include <iostream>

#include "Demuxer.h"
#include "Packet.h"
#include "MediaInfo.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
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
    int ret = avformat_open_input(&mFormatCtx, url.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open input: " << url << "ret: " << ret << "\n";
        return false;
    }

    ret = avformat_find_stream_info(mFormatCtx, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to find stream info\n";
        return false;
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
        mediaInfo.mVideoFrameRate = frameRate.num && frameRate.den ?
                                    static_cast<int>(av_q2d(frameRate) + 0.5) : 0;
    }

    return mAudioStreamIndex != -1 || mVideoStreamIndex != -1;
}

void Demuxer::start() {
    if (mRunning) return;
    mStopRequested = false;
    mRunning = true;
    mThread = std::thread(&Demuxer::demuxLoop, this);
}

void Demuxer::pause() {
    mPaused = true;
}

void Demuxer::resume() {
    mPaused = false;
    mCond.notify_all();
}

bool Demuxer::seek(int64_t timestampMs) {
    std::lock_guard<std::mutex> lock(mMutex);
    mSeeking = true;

    int64_t seekTarget = static_cast<int64_t>(timestampMs * AV_TIME_BASE / 1000);
    int ret = avformat_seek_file(mFormatCtx, -1, INT64_MIN, seekTarget, INT64_MAX, 0);
    if (ret < 0) {
        std::cerr << "av_seek_frame failed: " << ret << std::endl;
        return false;
    }
    avformat_flush(mFormatCtx);

    // 清空队列避免旧数据
    mAudioQueue->clear();
    mVideoQueue->clear();

    mSeeking = false;
    return true;
}

void Demuxer::stop() {
    if (!mRunning) return;
    mStopRequested = true;
    resume();  // 防止阻塞在 paused
    if (mThread.joinable()) {
        mThread.join();
    }
    if (mFormatCtx) {
        avformat_flush(mFormatCtx);
        avformat_close_input(&mFormatCtx);
        avformat_free_context(mFormatCtx);
        mFormatCtx = nullptr;
    }
    mRunning = false;
}

void Demuxer::demuxLoop() {
    AVPacket* pkt = av_packet_alloc();

    while (!mStopRequested) {
        // 暂停逻辑
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, [this] {
            return !mPaused || mStopRequested;
        });
        lock.unlock();

        if (mStopRequested || mSeeking) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int ret = av_read_frame(mFormatCtx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                std::cerr << "End of file\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
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

} // namespace yffplayer
