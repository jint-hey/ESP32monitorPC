#ifndef HARDWARE_PACKET_ENCODER_H
#define HARDWARE_PACKET_ENCODER_H

#include "HardwareMonitor.h"

#include <cstdint>
#include <vector>

/*
===============================================================================
HARDWARE PACKET ENCODER RULES
===============================================================================

Maximum GPU count:

    2

Valid GPU count values:

    0
    1
    2

If usage.gpus contains more than 2 entries,
only entries 0 and 1 are transmitted.

HardwareUsage payload:

    uint16 CPU_x100
    uint16 Memory_x100
    uint8  GPU_Count

    Repeated GPU_Count times:

        uint8  GPU_ID
        uint16 GPU_Usage_x100

Examples:

    No GPU:

        CPU
        Memory
        GPU_Count = 0

    One GPU:

        CPU
        Memory
        GPU_Count = 1

        GPU_ID = 0
        GPU_Usage

    Two GPUs:

        CPU
        Memory
        GPU_Count = 2

        GPU_ID = 0
        GPU_Usage

        GPU_ID = 1
        GPU_Usage

HardwareInfo payload:

    uint8 GPU_Count

    For each GPU:

        uint8  GPU_ID
        uint16 GPU_Name_Length
        uint8[] UTF8_GPU_Name

===============================================================================
*/

class HardwarePacketEncoder
{
public:
    static std::vector<std::uint8_t>
        BuildUsagePayload(
            const HardwareUsage& usage
        );

    static std::vector<std::uint8_t>
        BuildInfoPayload(
            const HardwareUsage& usage
        );

private:
    static constexpr std::size_t
        MAX_GPU_COUNT = 2;

    static constexpr std::size_t
        MAX_GPU_NAME_UTF8_LENGTH = 120;

    static std::uint16_t
        ScalePercentage(
            double percentage
        );
};

#endif
