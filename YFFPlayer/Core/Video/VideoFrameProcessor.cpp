#include "VideoFrameProcessor.h"

extern "C" {
#include <libavutil/imgutils.h>
}

namespace yffplayer {
VideoFrameProcessor::~VideoFrameProcessor() {
    if (mSwsCtx) {
        sws_freeContext(mSwsCtx);
        mSwsCtx = nullptr;
    }
}

std::unique_ptr<VideoFrame> VideoFrameProcessor::processAudioFrame(
    const std::shared_ptr<FrameHandle> frameHandle) {
    AVFrame* frame = frameHandle->getFrame();
    if (!frame) {
        return nullptr;
    }
    AVPixelFormat sourceFormat = (AVPixelFormat)frame->format;
    int64_t pts = frame->pts;
    int64_t duration = frame->duration;
    if (sourceFormat != AV_PIX_FMT_YUV420P && sourceFormat != AV_PIX_FMT_NV12 &&
        sourceFormat != AV_PIX_FMT_RGB24 && sourceFormat != AV_PIX_FMT_VIDEOTOOLBOX) {
        AVFrame* nv12Frame = av_frame_alloc();
        if (!mSwsCtx) {
            mSwsCtx = sws_getContext(frame->width, frame->height, sourceFormat, frame->width,
                                     frame->height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr,
                                     nullptr, nullptr);
        }
        av_image_alloc(nv12Frame->data, nv12Frame->linesize, frame->width, frame->height,
                       AV_PIX_FMT_NV12, 1);
        sws_scale(mSwsCtx, frame->data, frame->linesize, 0, frame->height, nv12Frame->data,
                  nv12Frame->linesize);

        std::array<int, 4> lineSize;
        int linesizes[4] = {0};
        av_image_fill_linesizes(linesizes, AV_PIX_FMT_NV12, frame->width);
        lineSize = {linesizes[0], 0, 0, 0};

        int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_NV12, frame->width, frame->height, 1);
        std::vector<uint8_t> data(bufferSize);
        av_image_copy_to_buffer(data.data(), bufferSize, nv12Frame->data, nv12Frame->linesize,
                                AV_PIX_FMT_NV12, frame->width, frame->height, 1);

        nv12Frame->pts = pts;
        nv12Frame->duration = duration;
        AVFrame* clonedFrame = av_frame_alloc();
        av_frame_ref(clonedFrame, nv12Frame);
        if (clonedFrame) {
            clonedFrame->pts = pts;
            clonedFrame->duration = duration;
            auto videoFrame = std::make_unique<VideoFrame>(clonedFrame);
            return videoFrame;
        }
        return nullptr;
    } else {
        AVFrame* clonedFrame = av_frame_alloc();
        av_frame_ref(clonedFrame, frame);
        if (clonedFrame) {
            clonedFrame->pts = pts;
            clonedFrame->duration = duration;
            auto videoFrame = std::make_unique<VideoFrame>(clonedFrame);
            return videoFrame;
        }
        return nullptr;
    }
}
}  // namespace yffplayer
