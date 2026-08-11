#include "hardware_state.hpp"

#include <array>

namespace
{
    uint16_t ReadUInt16Le(const uint8_t* data)
    {
        return static_cast<uint16_t>(
            static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8)
            );
    }

    uint16_t ClampPercentage(uint16_t value)
    {
        return value > 10000 ? 10000 : value;
    }
}

HardwareStateStore::HardwareStateStore()
{
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_ != nullptr);
}

void HardwareStateStore::Reset()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_ = HardwareSnapshot{};
    xSemaphoreGive(mutex_);
}

bool HardwareStateStore::ApplyPacket(
    const pc_protocol::Packet& packet
)
{
    switch (packet.type)
    {
    case pc_protocol::TYPE_HARDWARE_INFO:
        return ApplyHardwareInfo(packet);

    case pc_protocol::TYPE_HARDWARE_USAGE:
        return ApplyHardwareUsage(packet);

    default:
        return false;
    }
}

HardwareSnapshot HardwareStateStore::Copy() const
{
    HardwareSnapshot snapshot;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    snapshot = state_;
    xSemaphoreGive(mutex_);

    return snapshot;
}

bool HardwareStateStore::ApplyHardwareInfo(
    const pc_protocol::Packet& packet
)
{
    if (packet.payloadLength < 1)
    {
        return false;
    }

    const uint8_t gpuCount = packet.payload[0];

    if (gpuCount > MAX_GPU_COUNT)
    {
        return false;
    }

    std::array<
        std::array<char, MAX_GPU_NAME_LENGTH + 1>,
        MAX_GPU_COUNT
    > names{};
    std::array<bool, MAX_GPU_COUNT> found{};
    std::size_t offset = 1;

    for (std::size_t index = 0;
        index < gpuCount;
        ++index)
    {
        if (offset + 3 > packet.payloadLength)
        {
            return false;
        }

        const uint8_t gpuId = packet.payload[offset];
        const uint16_t nameLength =
            ReadUInt16Le(packet.payload.data() + offset + 1);
        offset += 3;

        if (gpuId >= gpuCount ||
            found[gpuId] ||
            nameLength > MAX_GPU_NAME_LENGTH ||
            offset + nameLength > packet.payloadLength)
        {
            return false;
        }

        for (std::size_t byteIndex = 0;
            byteIndex < nameLength;
            ++byteIndex)
        {
            const uint8_t value =
                packet.payload[offset + byteIndex];

            names[gpuId][byteIndex] =
                value == 0
                ? '?'
                : static_cast<char>(value);
        }

        names[gpuId][nameLength] = '\0';
        found[gpuId] = true;
        offset += nameLength;
    }

    if (offset != packet.payloadLength)
    {
        return false;
    }

    for (std::size_t index = 0;
        index < gpuCount;
        ++index)
    {
        if (!found[index])
        {
            return false;
        }
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.gpuCount = gpuCount;
    state_.gpuNames = names;
    state_.lastPacketSequence = packet.sequence;
    xSemaphoreGive(mutex_);

    return true;
}

bool HardwareStateStore::ApplyHardwareUsage(
    const pc_protocol::Packet& packet
)
{
    if (packet.payloadLength < 5)
    {
        return false;
    }

    const uint8_t gpuCount = packet.payload[4];
    const std::size_t expectedLength =
        5 + static_cast<std::size_t>(gpuCount) * 3;

    if (gpuCount > MAX_GPU_COUNT ||
        packet.payloadLength != expectedLength)
    {
        return false;
    }

    std::array<uint16_t, MAX_GPU_COUNT> usages{};
    std::array<bool, MAX_GPU_COUNT> found{};
    std::size_t offset = 5;

    for (std::size_t index = 0;
        index < gpuCount;
        ++index)
    {
        const uint8_t gpuId = packet.payload[offset];

        if (gpuId >= gpuCount ||
            found[gpuId])
        {
            return false;
        }

        usages[gpuId] = ClampPercentage(
            ReadUInt16Le(packet.payload.data() + offset + 1)
        );

        found[gpuId] = true;
        offset += 3;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    state_.cpuUsageX100 = ClampPercentage(
        ReadUInt16Le(packet.payload.data())
    );

    state_.memoryUsageX100 = ClampPercentage(
        ReadUInt16Le(packet.payload.data() + 2)
    );

    state_.gpuCount = gpuCount;
    state_.gpuUsageX100 = usages;

    state_.lastPacketSequence = packet.sequence;
    state_.hasUsage = true;
    ++state_.updateCount;

    xSemaphoreGive(mutex_);
    return true;
}
