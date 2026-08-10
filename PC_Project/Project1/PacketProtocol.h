#ifndef PACKET_PROTOCOL_H
#define PACKET_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <vector>

/*
===============================================================================
PC <-> ESP32 SERIAL PACKET PROTOCOL
===============================================================================

Serial configuration:

    Baud rate : 115200
    Data bits : 8
    Parity    : None
    Stop bits : 1
    Flow Ctrl : None

Packet byte order:

    All multi-byte integers use LITTLE ENDIAN.

Generic packet format:

    Offset    Size        Field
    ----------------------------------------------------------------
    0         1 byte      SOF1 = 0xAA
    1         1 byte      SOF2 = 0x55
    2         1 byte      Protocol Version
    3         1 byte      Packet Type
    4         1 byte      Flags
    5         2 bytes     Sequence Number
    7         2 bytes     Payload Length
    9         N bytes     Payload
    9 + N     2 bytes     CRC16

Total packet size:

    11 + PayloadLength

Protocol Version:

    Version 1 = 0x01

Maximum payload:

    512 bytes

CRC:

    Algorithm  : CRC-16/CCITT-FALSE
    Polynomial : 0x1021
    Initial    : 0xFFFF
    RefIn      : false
    RefOut     : false

CRC calculation range:

    Version
    Packet Type
    Flags
    Sequence
    Payload Length
    Payload

The two SOF bytes 0xAA 0x55 are NOT included in CRC.

-------------------------------------------------------------------------------
HardwareUsage payload
-------------------------------------------------------------------------------

CPU and memory percentages are stored as:

    actual percentage * 100

Examples:

    0.00%   ->     0
    12.34%  ->  1234
    100.00% -> 10000

HardwareUsage payload:

    Offset    Size        Field
    ----------------------------------------------------------------
    0         2 bytes     CPU usage x 100
    2         2 bytes     Memory usage x 100
    4         1 byte      GPU Count

Then, for every GPU:

              1 byte      GPU ID
              2 bytes     GPU Usage x 100

GPU Count rules:

    GPU Count can ONLY be:

        0
        1
        2

    Maximum supported GPU count is 2.

Examples:

    0 GPUs:
        Payload size = 5 bytes

    1 GPU:
        Payload size = 8 bytes

    2 GPUs:
        Payload size = 11 bytes

If Windows reports more than 2 GPUs,
only the first two physical GPUs are transmitted.

-------------------------------------------------------------------------------
HardwareInfo payload
-------------------------------------------------------------------------------

HardwareInfo is normally sent once when communication starts.

Format:

    1 byte      GPU Count

For each GPU:

    1 byte      GPU ID
    2 bytes     UTF-8 GPU name length
    N bytes     UTF-8 GPU name

GPU Count is also limited to 0, 1 or 2.

-------------------------------------------------------------------------------
Packet loss policy
-------------------------------------------------------------------------------

HardwareUsage is telemetry data.

Telemetry packets do NOT require ACK and are NOT retransmitted.

Reason:

    Old CPU/GPU/RAM usage data becomes obsolete quickly.
    Retransmitting an old telemetry packet is not useful.

Commands can use ACK in a future protocol extension.

===============================================================================
*/

namespace PacketProtocol
{
    constexpr std::uint8_t SOF1 = 0xAA;
    constexpr std::uint8_t SOF2 = 0x55;

    constexpr std::uint8_t VERSION = 0x01;

    constexpr std::size_t MAX_PAYLOAD_SIZE = 512;

    constexpr std::size_t FIXED_HEADER_SIZE = 9;
    constexpr std::size_t PACKET_OVERHEAD = 11;
}

enum class PacketType : std::uint8_t
{
    HardwareInfo = 0x01,
    HardwareUsage = 0x02,

    Ping = 0x10,
    Pong = 0x11,

    Command = 0x20,
    Ack = 0x21,

    Error = 0x7F
};

struct Packet
{
    PacketType type = PacketType::Error;

    std::uint8_t flags = 0;

    std::uint16_t sequence = 0;

    std::vector<std::uint8_t> payload;
};

class PacketEncoder
{
public:
    static bool Encode(
        const Packet& packet,
        std::vector<std::uint8_t>& output
    );

    static std::uint16_t CalculateCRC16(
        const std::uint8_t* data,
        std::size_t length
    );
};

class PacketParser
{
public:
    void PushBytes(
        const std::uint8_t* data,
        std::size_t length
    );

    bool TryGetPacket(
        Packet& packet
    );

    void Clear();

private:
    std::vector<std::uint8_t> buffer_;
};

#endif
