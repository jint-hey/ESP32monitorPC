#include "PacketProtocol.h"

#include <algorithm>

namespace
{
    void AppendUInt16LE(
        std::vector<std::uint8_t>& buffer,
        std::uint16_t value
    )
    {
        buffer.push_back(
            static_cast<std::uint8_t>(
                value & 0xFF
                )
        );

        buffer.push_back(
            static_cast<std::uint8_t>(
                (value >> 8) & 0xFF
                )
        );
    }

    std::uint16_t ReadUInt16LE(
        const std::uint8_t* data
    )
    {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[0]) |
            (
                static_cast<std::uint16_t>(data[1])
                << 8
                )
            );
    }
}

// ============================================================================
// CRC-16/CCITT-FALSE
// ============================================================================

std::uint16_t PacketEncoder::CalculateCRC16(
    const std::uint8_t* data,
    std::size_t length
)
{
    std::uint16_t crc = 0xFFFF;

    for (std::size_t i = 0; i < length; ++i)
    {
        crc ^=
            static_cast<std::uint16_t>(
                data[i]
                ) << 8;

        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x8000) != 0)
            {
                crc =
                    static_cast<std::uint16_t>(
                        (crc << 1) ^ 0x1021
                        );
            }
            else
            {
                crc =
                    static_cast<std::uint16_t>(
                        crc << 1
                        );
            }
        }
    }

    return crc;
}

// ============================================================================
// Encode packet
// ============================================================================

bool PacketEncoder::Encode(
    const Packet& packet,
    std::vector<std::uint8_t>& output
)
{
    if (packet.payload.size() >
        PacketProtocol::MAX_PAYLOAD_SIZE)
    {
        return false;
    }

    output.clear();

    output.reserve(
        PacketProtocol::PACKET_OVERHEAD +
        packet.payload.size()
    );

    // SOF
    output.push_back(
        PacketProtocol::SOF1
    );

    output.push_back(
        PacketProtocol::SOF2
    );

    // Version
    output.push_back(
        PacketProtocol::VERSION
    );

    // Type
    output.push_back(
        static_cast<std::uint8_t>(
            packet.type
            )
    );

    // Flags
    output.push_back(
        packet.flags
    );

    // Sequence
    AppendUInt16LE(
        output,
        packet.sequence
    );

    // Payload length
    AppendUInt16LE(
        output,
        static_cast<std::uint16_t>(
            packet.payload.size()
            )
    );

    // Payload
    output.insert(
        output.end(),
        packet.payload.begin(),
        packet.payload.end()
    );

    /*
        CRC starts from Version.

        output[0] = 0xAA
        output[1] = 0x55
        output[2] = Version

        CRC length:

            7 bytes header excluding SOF
            +
            payload size
    */

    const std::uint16_t crc =
        CalculateCRC16(
            output.data() + 2,
            7 + packet.payload.size()
        );

    AppendUInt16LE(
        output,
        crc
    );

    return true;
}

// ============================================================================
// Parser
// ============================================================================

void PacketParser::PushBytes(
    const std::uint8_t* data,
    std::size_t length
)
{
    if (data == nullptr ||
        length == 0)
    {
        return;
    }

    buffer_.insert(
        buffer_.end(),
        data,
        data + length
    );
}

void PacketParser::Clear()
{
    buffer_.clear();
}

bool PacketParser::TryGetPacket(
    Packet& packet
)
{
    while (true)
    {
        if (buffer_.size() < 2)
        {
            return false;
        }

        // --------------------------------------------------------------------
        // Find packet start: AA 55
        // --------------------------------------------------------------------

        std::size_t start =
            buffer_.size();

        for (std::size_t i = 0;
            i + 1 < buffer_.size();
            ++i)
        {
            if (buffer_[i] ==
                PacketProtocol::SOF1 &&
                buffer_[i + 1] ==
                PacketProtocol::SOF2)
            {
                start = i;
                break;
            }
        }

        if (start == buffer_.size())
        {
            /*
                Keep a trailing 0xAA.

                Example:

                    Current read ends with:
                        ... AA

                    Next read begins with:
                        55 ...

                That can still form AA 55.
            */

            const bool keepLastAA =
                !buffer_.empty() &&
                buffer_.back() ==
                PacketProtocol::SOF1;

            buffer_.clear();

            if (keepLastAA)
            {
                buffer_.push_back(
                    PacketProtocol::SOF1
                );
            }

            return false;
        }

        if (start > 0)
        {
            buffer_.erase(
                buffer_.begin(),
                buffer_.begin() + start
            );
        }

        // Need complete fixed header.
        if (buffer_.size() <
            PacketProtocol::FIXED_HEADER_SIZE)
        {
            return false;
        }

        // --------------------------------------------------------------------
        // Validate protocol version
        // --------------------------------------------------------------------

        if (buffer_[2] !=
            PacketProtocol::VERSION)
        {
            // Drop one byte and search again.
            buffer_.erase(
                buffer_.begin()
            );

            continue;
        }

        // --------------------------------------------------------------------
        // Payload length
        // --------------------------------------------------------------------

        const std::uint16_t payloadLength =
            ReadUInt16LE(
                buffer_.data() + 7
            );

        if (payloadLength >
            PacketProtocol::MAX_PAYLOAD_SIZE)
        {
            // Invalid packet.
            buffer_.erase(
                buffer_.begin()
            );

            continue;
        }

        const std::size_t totalSize =
            PacketProtocol::PACKET_OVERHEAD +
            payloadLength;

        if (buffer_.size() < totalSize)
        {
            // Packet is incomplete.
            return false;
        }

        // --------------------------------------------------------------------
        // CRC
        // --------------------------------------------------------------------

        const std::size_t crcOffset =
            9 + payloadLength;

        const std::uint16_t receivedCRC =
            ReadUInt16LE(
                buffer_.data() + crcOffset
            );

        const std::uint16_t calculatedCRC =
            PacketEncoder::CalculateCRC16(
                buffer_.data() + 2,
                7 + payloadLength
            );

        if (receivedCRC != calculatedCRC)
        {
            // Corrupted packet.
            // Drop one byte and search for a new AA 55.
            buffer_.erase(
                buffer_.begin()
            );

            continue;
        }

        // --------------------------------------------------------------------
        // Complete valid packet
        // --------------------------------------------------------------------

        packet.type =
            static_cast<PacketType>(
                buffer_[3]
                );

        packet.flags =
            buffer_[4];

        packet.sequence =
            ReadUInt16LE(
                buffer_.data() + 5
            );

        packet.payload.assign(
            buffer_.begin() + 9,
            buffer_.begin() + 9 +
            payloadLength
        );

        // Remove parsed packet.
        buffer_.erase(
            buffer_.begin(),
            buffer_.begin() +
            totalSize
        );

        return true;
    }
}
