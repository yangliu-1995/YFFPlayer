#pragma once

extern "C" {
#include <libavcodec/packet.h>
}

namespace yffplayer {
struct Packet {
    AVPacket *mPacket = nullptr;

    explicit Packet(AVPacket *packet) : mPacket(packet) {}

    Packet(const Packet &other) {
        if (other.mPacket) {
            mPacket = av_packet_clone(other.mPacket);
        }
    }

    Packet(Packet &&other) noexcept : mPacket(other.mPacket) {
        other.mPacket = nullptr;
    }

    Packet& operator=(const Packet &other) {
        if (this != &other) {
            if (mPacket) {
                av_packet_free(&mPacket);
            }
            mPacket = other.mPacket ? av_packet_clone(other.mPacket) : nullptr;
        }
        return *this;
    }

    Packet& operator=(Packet &&other) noexcept {
        if (this != &other) {
            if (mPacket) {
                av_packet_free(&mPacket);
            }
            mPacket = other.mPacket;
            other.mPacket = nullptr;
        }
        return *this;
    }

    bool isKeyFrame() const {
        return mPacket->flags & AV_PKT_FLAG_KEY;
    }

    ~Packet() {
        if (mPacket) {
            av_packet_free(&mPacket);
        }
    }
};
} // namespace yffplayer
