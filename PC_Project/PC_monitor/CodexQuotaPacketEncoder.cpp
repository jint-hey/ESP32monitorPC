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
    if (snapshot.quota.valid)
    {
        flags |= 0x01;
    }
    if (snapshot.rateLimitReached)
    {
        flags |= 0x02;
    }
    if (snapshot.stale)
    {
        flags |= 0x04;
    }
    payload[2] = flags;

    WriteUInt64Le(payload, 3, snapshot.collectedAtUnixSeconds);
    if (snapshot.quota.valid)
    {
        WriteUInt16Le(payload, 11, snapshot.quota.usedPercentX100);
        WriteUInt16Le(payload, 13, snapshot.quota.remainingPercentX100);
        WriteUInt32Le(payload, 15, snapshot.quota.windowDurationMinutes);
        WriteUInt64Le(payload, 19, snapshot.quota.resetsAtUnixSeconds);
    }

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
