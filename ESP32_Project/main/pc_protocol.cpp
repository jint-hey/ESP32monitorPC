#include "pc_protocol.hpp"

#include <cstring>

namespace
{
    uint16_t ReadUInt16Le(const uint8_t* data)
    {
        return static_cast<uint16_t>(
            static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8)
            );
    }

    void WriteUInt16Le(
        uint8_t* destination,
        uint16_t value
    )
    {
        destination[0] = static_cast<uint8_t>(value & 0xFF);
        destination[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }
}

uint16_t pc_protocol::CalculateCrc16(
    const uint8_t* data,
    std::size_t length
)
{
    uint16_t crc = 0xFFFF;

    for (std::size_t index = 0;
        index < length;
        ++index)
    {
        crc ^= static_cast<uint16_t>(data[index]) << 8;

        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x8000) != 0)
            {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            }
            else
            {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }

    return crc;
}

std::size_t pc_protocol::EncodePacket(
    uint8_t type,
    uint8_t flags,
    uint16_t sequence,
    const uint8_t* payload,
    std::size_t payloadLength,
    uint8_t* output,
    std::size_t outputCapacity
)
{
    const std::size_t totalLength =
        PACKET_OVERHEAD + payloadLength;

    if (output == nullptr ||
        payloadLength > MAX_PAYLOAD_SIZE ||
        totalLength > outputCapacity ||
        (payloadLength > 0 && payload == nullptr))
    {
        return 0;
    }

    output[0] = SOF1;
    output[1] = SOF2;
    output[2] = VERSION;
    output[3] = type;
    output[4] = flags;
    WriteUInt16Le(output + 5, sequence);
    WriteUInt16Le(
        output + 7,
        static_cast<uint16_t>(payloadLength)
    );

    if (payloadLength > 0)
    {
        std::memcpy(
            output + FIXED_HEADER_SIZE,
            payload,
            payloadLength
        );
    }

    const uint16_t crc = CalculateCrc16(
        output + 2,
        7 + payloadLength
    );

    WriteUInt16Le(
        output + FIXED_HEADER_SIZE + payloadLength,
        crc
    );

    return totalLength;
}

void pc_protocol::Parser::PushBytes(
    const uint8_t* data,
    std::size_t length
)
{
    buffer_.Push(data, length);
}

bool pc_protocol::Parser::TryGetPacket(
    Packet& packet
)
{
    while (true)
    {
        if (buffer_.Size() < 2)
        {
            return false;
        }

        while (buffer_.Size() >= 2 &&
            (buffer_.Peek(0) != SOF1 ||
                buffer_.Peek(1) != SOF2))
        {
            buffer_.Discard(1);
        }

        if (buffer_.Size() < FIXED_HEADER_SIZE)
        {
            return false;
        }

        if (buffer_.Peek(2) != VERSION)
        {
            ++formatErrorCount_;
            buffer_.Discard(1);
            continue;
        }

        const uint16_t payloadLength =
            static_cast<uint16_t>(
                buffer_.Peek(7) |
                (static_cast<uint16_t>(buffer_.Peek(8)) << 8)
                );

        if (payloadLength > MAX_PAYLOAD_SIZE)
        {
            ++formatErrorCount_;
            buffer_.Discard(1);
            continue;
        }

        const std::size_t totalLength =
            PACKET_OVERHEAD + payloadLength;

        if (buffer_.Size() < totalLength)
        {
            return false;
        }

        buffer_.Copy(0, packetBuffer_.data(), totalLength);

        const uint16_t receivedCrc =
            ReadUInt16Le(
                packetBuffer_.data() +
                FIXED_HEADER_SIZE +
                payloadLength
            );

        const uint16_t calculatedCrc =
            CalculateCrc16(
                packetBuffer_.data() + 2,
                7 + payloadLength
            );

        if (receivedCrc != calculatedCrc)
        {
            ++crcErrorCount_;
            buffer_.Discard(1);
            continue;
        }

        packet.type = packetBuffer_[3];
        packet.flags = packetBuffer_[4];
        packet.sequence = ReadUInt16Le(packetBuffer_.data() + 5);
        packet.payloadLength = payloadLength;

        if (payloadLength > 0)
        {
            std::memcpy(
                packet.payload.data(),
                packetBuffer_.data() + FIXED_HEADER_SIZE,
                payloadLength
            );
        }

        buffer_.Discard(totalLength);
        return true;
    }
}
