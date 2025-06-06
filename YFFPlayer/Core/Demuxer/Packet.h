#pragma once

extern "C" {
#include <libavcodec/packet.h>
}

namespace yffplayer {
struct Packet {
    AVPacket *packet_ = nullptr;

    explicit Packet(AVPacket *packet) : packet_(packet) {}

    Packet(const Packet &other) {
        if (other.packet_) {
            packet_ = av_packet_clone(other.packet_);
        }
    }

    Packet(Packet &&other) noexcept : packet_(other.packet_) { other.packet_ = nullptr; }

    Packet &operator=(const Packet &other) {
        if (this != &other) {
            if (packet_) {
                av_packet_free(&packet_);
            }
            packet_ = other.packet_ ? av_packet_clone(other.packet_) : nullptr;
        }
        return *this;
    }

    Packet &operator=(Packet &&other) noexcept {
        if (this != &other) {
            if (packet_) {
                av_packet_free(&packet_);
            }
            packet_ = other.packet_;
            other.packet_ = nullptr;
        }
        return *this;
    }

    bool isKeyFrame() const { return packet_->flags & AV_PKT_FLAG_KEY; }

    ~Packet() {
        if (packet_) {
            av_packet_free(&packet_);
        }
    }
};
}  // namespace yffplayer
