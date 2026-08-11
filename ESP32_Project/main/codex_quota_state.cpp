#include "codex_quota_state.hpp"

namespace
{
constexpr uint8_t PAYLOAD_SCHEMA_VERSION = 2;
constexpr std::size_t PAYLOAD_SIZE = 27;

uint16_t ReadUInt16Le(const uint8_t* data)
{
    return static_cast<uint16_t>(
        static_cast<uint16_t>(data[0]) |
        (static_cast<uint16_t>(data[1]) << 8));
}

uint32_t ReadUInt32Le(const uint8_t* data)
{
    uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index)
    {
        value |= static_cast<uint32_t>(data[index]) << (index * 8U);
    }
    return value;
}

uint64_t ReadUInt64Le(const uint8_t* data)
{
    uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index)
    {
        value |= static_cast<uint64_t>(data[index]) << (index * 8U);
    }
    return value;
}

uint16_t ClampPercentage(const uint16_t value)
{
    return value > 10000U ? 10000U : value;
}

}

CodexQuotaStateStore::CodexQuotaStateStore()
{
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_ != nullptr);
}

void CodexQuotaStateStore::Reset()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_ = CodexQuotaSnapshot{};
    xSemaphoreGive(mutex_);
}

bool CodexQuotaStateStore::ApplyPacket(const pc_protocol::Packet& packet)
{
    if (packet.type != pc_protocol::TYPE_CODEX_QUOTA ||
        packet.payloadLength != PAYLOAD_SIZE ||
        packet.payload[0] != PAYLOAD_SCHEMA_VERSION ||
        packet.payload[1] > static_cast<uint8_t>(CodexQuotaStatus::CollectorError))
    {
        return false;
    }

    const uint8_t flags = packet.payload[2];
    CodexQuotaSnapshot next;
    next.status = static_cast<CodexQuotaStatus>(packet.payload[1]);
    next.rateLimitReached = (flags & 0x02U) != 0;
    next.collectedAtUnixSeconds = ReadUInt64Le(packet.payload.data() + 3);
    next.quota.valid = (flags & 0x01U) != 0;
    if (next.quota.valid)
    {
        next.quota.usedPercentX100 =
            ClampPercentage(ReadUInt16Le(packet.payload.data() + 11));
        next.quota.remainingPercentX100 =
            ClampPercentage(ReadUInt16Le(packet.payload.data() + 13));
        next.quota.windowDurationMinutes =
            ReadUInt32Le(packet.payload.data() + 15);
        next.quota.resetsAtUnixSeconds =
            ReadUInt64Le(packet.payload.data() + 19);
    }
    next.lastPacketSequence = packet.sequence;
    next.hasPacket = true;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    next.updateCount = state_.updateCount + 1U;
    state_ = next;
    xSemaphoreGive(mutex_);
    return true;
}

CodexQuotaSnapshot CodexQuotaStateStore::Copy() const
{
    CodexQuotaSnapshot snapshot;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    snapshot = state_;
    xSemaphoreGive(mutex_);
    return snapshot;
}
