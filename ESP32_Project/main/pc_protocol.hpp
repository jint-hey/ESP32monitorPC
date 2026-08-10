#pragma once

#include "byte_ring_buffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pc_protocol
{
    inline constexpr uint8_t SOF1 = 0xAA;
    inline constexpr uint8_t SOF2 = 0x55;
    inline constexpr uint8_t VERSION = 0x01;

    inline constexpr uint8_t TYPE_HARDWARE_INFO = 0x01;
    inline constexpr uint8_t TYPE_HARDWARE_USAGE = 0x02;
    inline constexpr uint8_t TYPE_CODEX_QUOTA = 0x03;
    inline constexpr uint8_t TYPE_PING = 0x10;
    inline constexpr uint8_t TYPE_PONG = 0x11;

    inline constexpr std::size_t MAX_PAYLOAD_SIZE = 512;
    inline constexpr std::size_t FIXED_HEADER_SIZE = 9;
    inline constexpr std::size_t PACKET_OVERHEAD = 11;
    inline constexpr std::size_t MAX_PACKET_SIZE =
        MAX_PAYLOAD_SIZE + PACKET_OVERHEAD;

    struct Packet
    {
        uint8_t type = 0;
        uint8_t flags = 0;
        uint16_t sequence = 0;
        uint16_t payloadLength = 0;
        std::array<uint8_t, MAX_PAYLOAD_SIZE> payload{};
    };

    uint16_t CalculateCrc16(
        const uint8_t* data,
        std::size_t length
    );

    std::size_t EncodePacket(
        uint8_t type,
        uint8_t flags,
        uint16_t sequence,
        const uint8_t* payload,
        std::size_t payloadLength,
        uint8_t* output,
        std::size_t outputCapacity
    );

    class Parser
    {
    public:
        void PushBytes(
            const uint8_t* data,
            std::size_t length
        );

        bool TryGetPacket(Packet& packet);

        std::size_t CrcErrorCount() const
        {
            return crcErrorCount_;
        }

        std::size_t FormatErrorCount() const
        {
            return formatErrorCount_;
        }

        std::size_t OverflowCount() const
        {
            return buffer_.OverflowCount();
        }

    private:
        ByteRingBuffer<2048> buffer_;
        std::array<uint8_t, MAX_PACKET_SIZE> packetBuffer_{};
        std::size_t crcErrorCount_ = 0;
        std::size_t formatErrorCount_ = 0;
    };
}
