#include "AudioDecoder.h"

#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#endif

#include "Log.h"
#include "Packet.h"

namespace yffplayer {

AudioDecoder::AudioDecoder(std::shared_ptr<PacketQueue> packetQueue,
                           std::shared_ptr<FrameQueue<FrameHandle>> frameQueue)
    : packetQueue_(std::move(packetQueue)), frameQueue_(std::move(frameQueue)) {}

AudioDecoder::~AudioDecoder() {
    stop();
    LogInfo << "~AudioDecoder";
}

bool AudioDecoder::open(AVCodecParameters* codecParams, AVRational timeBase) {
    timeBase_ = timeBase;

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        LogInfo << "Audio codec not found";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        LogInfo << "Failed to allocate audio codec context";
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecParams) < 0) {
        LogInfo << "Failed to copy codec parameters";
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        LogInfo << "Failed to open audio codec";
        return false;
    }

    // 移除重采样逻辑，decoder只负责解码

    return true;
}

void AudioDecoder::start() {
    isRunning_ = true;
    paused_ = false;
    decodeThread_ = std::thread(&AudioDecoder::decodeLoop, this);
    LogInfo << "AudioDecoder started";
}

void AudioDecoder::stop() {
    isRunning_ = false;
    resume();  // 防止线程阻塞在暂停状态
    if (decodeThread_.joinable()) {
        decodeThread_.join();
    }
    if (codecCtx_ && avcodec_is_open(codecCtx_)) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
}

void AudioDecoder::pause() { paused_ = true; }

void AudioDecoder::resume() {
    paused_ = false;
    cond_.notify_all();
}

void AudioDecoder::flush() {
    frameQueue_->clear();
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }
}

AVSampleFormat AudioDecoder::getFormat() const {
    if (!codecCtx_) {
        return AV_SAMPLE_FMT_NONE;
    }
    return codecCtx_->sample_fmt;
}

int AudioDecoder::getSampleRate() const {
    if (!codecCtx_) {
        return 0;
    }
    return codecCtx_->sample_rate;
}
int AudioDecoder::getNbChannels() const {
    if (!codecCtx_) {
        return 0;
    }
    return codecCtx_->ch_layout.nb_channels;
}

void AudioDecoder::decodeLoop() {
#if defined(__APPLE__)
    pthread_setname_np("com.yffplayer.decoder.audio");
#endif
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame) {
        LogInfo << "Failed to allocate packet or frame";
        av_packet_free(&packet);
        av_frame_free(&frame);
        return;
    }

    while (isRunning_) {  // 使用mIsRunning替代!stopped_
        // 处理暂停逻辑
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {
            return !paused_;  // 使用!mIsRunning替代mStopped
        });
        lock.unlock();

        if (!isRunning_) {  // 使用!mIsRunning替代mStopped
            break;
        }

        auto pkt = packetQueue_->pop();
        if (!pkt) {
            continue;
        }

        auto avPacket = pkt->packet_;
        if (!avPacket) {
            av_packet_unref(pkt->packet_);  // 清理 Packet 内的 AVPacket
            continue;
        }

        int ret = avcodec_send_packet(codecCtx_, avPacket);
        if (ret < 0) {
            LogInfo << "Failed to send packet: " << av_err2str(ret);
            av_packet_unref(avPacket);
            continue;
        }

        while (true) {
            av_frame_unref(frame);  // 清理当前帧
            ret = avcodec_receive_frame(codecCtx_, frame);
            if (ret == 0) {
                AVFrame* frameClone = av_frame_alloc();
                av_frame_ref(frameClone, frame);
                if (!frameClone) {
                    LogInfo << "Failed to allocate frame clone";
                    continue;
                }
                if (frameClone->pts != AV_NOPTS_VALUE) {
                    frameClone->pts =
                        static_cast<int64_t>(frameClone->pts * av_q2d(timeBase_) * 1000);
                } else if (frameClone->best_effort_timestamp != AV_NOPTS_VALUE) {
                    frameClone->pts = static_cast<int64_t>(frameClone->best_effort_timestamp *
                                                           av_q2d(timeBase_) * 1000);
                }
                frameQueue_->push(std::make_shared<FrameHandle>(frameClone));
            } else if (ret == AVERROR(EAGAIN)) {
                break;  // 需要更多输入
            } else if (ret == AVERROR_EOF) {
                break;  // 解码结束
            } else {
                LogInfo << "Failed to receive frame: " << av_err2str(ret);
                break;
            }
        }

        av_packet_unref(avPacket);  // 清理当前包
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    LogInfo << "AudioDecoder thread ended";
}

}  // namespace yffplayer
