#pragma once

#include "pc_protocol.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <array>
#include <cstddef>
#include <cstdint>

inline constexpr std::size_t MAX_GPU_COUNT = 2;
inline constexpr std::size_t MAX_GPU_NAME_LENGTH = 120;

struct HardwareSnapshot
{
    uint16_t cpuUsageX100 = 0;
    uint16_t memoryUsageX100 = 0;
    uint8_t gpuCount = 0;
    std::array<uint16_t, MAX_GPU_COUNT> gpuUsageX100{};
    std::array<
        std::array<char, MAX_GPU_NAME_LENGTH + 1>,
        MAX_GPU_COUNT
    > gpuNames{};
    uint32_t updateCount = 0;
    uint16_t lastPacketSequence = 0;
    bool hasUsage = false;
};

class HardwareStateStore
{
public:
    HardwareStateStore();

    HardwareStateStore(
        const HardwareStateStore&
    ) = delete;

    HardwareStateStore& operator=(
        const HardwareStateStore&
        ) = delete;

    bool ApplyPacket(
        const pc_protocol::Packet& packet
    );

    HardwareSnapshot Copy() const;

private:
    bool ApplyHardwareInfo(
        const pc_protocol::Packet& packet
    );

    bool ApplyHardwareUsage(
        const pc_protocol::Packet& packet
    );

    mutable SemaphoreHandle_t mutex_ = nullptr;
    HardwareSnapshot state_{};
};
