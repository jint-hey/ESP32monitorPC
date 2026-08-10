#include "CodexQuotaPacketEncoder.h"

std::vector<std::uint8_t> CodexQuotaPacketEncoder::BuildPayload(
    const CodexQuotaSnapshot& snapshot
)
{
    std::vector<std::uint8_t> payload(
        CodexQuotaProtocol::PAYLOAD_SIZE,
        0
    );

    payload[0] = CodexQuotaProtocol::PAYLOAD_VERSION;
    payload[1] = static_cast<std::uint8_t>(snapshot.status);

    std::uint8_t flags = 0;
    if (snapshot.primary.valid)
    {
        flags |= 0x01;
    }
    if (snapshot.secondary.valid)
    {
        flags |= 0x02;
    }
    if (snapshot.rateLimitReached)
    {
        flags |= 0x04;
    }
    payload[2] = flags;

    WriteUInt64Le(payload, 3, snapshot.collectedAtUnixSeconds);
    WriteWindow(payload, 11, snapshot.primary);
    WriteWindow(payload, 27, snapshot.secondary);

    return payload;
}

void CodexQuotaPacketEncoder::WriteUInt16Le(
    std::vector<std::uint8_t>& payload,
    const std::size_t offset,
    const std::uint16_t value
)
{
    payload[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    payload[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void CodexQuotaPacketEncoder::WriteUInt32Le(
    std::vector<std::uint8_t>& payload,
    const std::size_t offset,
    const std::uint32_t value
)
{
    for (std::size_t byteIndex = 0; byteIndex < 4; ++byteIndex)
    {
        payload[offset + byteIndex] = static_cast<std::uint8_t>(
            (value >> (byteIndex * 8U)) & 0xFFU
        );
    }
}

void CodexQuotaPacketEncoder::WriteUInt64Le(
    std::vector<std::uint8_t>& payload,
    const std::size_t offset,
    const std::uint64_t value
)
{
    for (std::size_t byteIndex = 0; byteIndex < 8; ++byteIndex)
    {
        payload[offset + byteIndex] = static_cast<std::uint8_t>(
            (value >> (byteIndex * 8U)) & 0xFFU
        );
    }
}

void CodexQuotaPacketEncoder::WriteWindow(
    std::vector<std::uint8_t>& payload,
    const std::size_t offset,
    const CodexQuotaWindow& window
)
{
    if (!window.valid)
    {
        return;
    }

    WriteUInt16Le(payload, offset, window.usedPercentX100);
    WriteUInt16Le(payload, offset + 2, window.remainingPercentX100);
    WriteUInt32Le(payload, offset + 4, window.windowDurationMinutes);
    WriteUInt64Le(payload, offset + 8, window.resetsAtUnixSeconds);
}
