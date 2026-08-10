#include "HardwarePacketEncoder.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <string>

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

    std::string WideToUtf8(
        const std::wstring& value
    )
    {
        if (value.empty())
        {
            return {};
        }

        const int size =
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.c_str(),
                static_cast<int>(
                    value.size()
                    ),
                nullptr,
                0,
                nullptr,
                nullptr
            );

        if (size <= 0)
        {
            return {};
        }

        std::string result(
            static_cast<std::size_t>(size),
            '\0'
        );

        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            static_cast<int>(
                value.size()
                ),
            result.data(),
            size,
            nullptr,
            nullptr
        );

        return result;
    }

    std::string TruncateUtf8(
        const std::string& text,
        std::size_t maxBytes
    )
    {
        if (text.size() <= maxBytes)
        {
            return text;
        }

        std::size_t newSize =
            maxBytes;

        /*
            Do not cut inside a UTF-8 continuation byte.

            UTF-8 continuation bytes have the form:

                10xxxxxx
        */

        while (newSize > 0 &&
            newSize < text.size() &&
            (
                static_cast<unsigned char>(
                    text[newSize]
                    ) & 0xC0
                ) == 0x80)
        {
            --newSize;
        }

        return text.substr(
            0,
            newSize
        );
    }
}

// ============================================================================
// Percentage conversion
// ============================================================================

std::uint16_t
HardwarePacketEncoder::ScalePercentage(
    double percentage
)
{
    if (!std::isfinite(
        percentage))
    {
        percentage = 0.0;
    }

    if (percentage < 0.0)
    {
        percentage = 0.0;
    }

    if (percentage > 100.0)
    {
        percentage = 100.0;
    }

    return static_cast<std::uint16_t>(
        std::lround(
            percentage * 100.0
        )
        );
}

// ============================================================================
// Hardware usage
// ============================================================================

std::vector<std::uint8_t>
HardwarePacketEncoder::BuildUsagePayload(
    const HardwareUsage& usage
)
{
    std::vector<std::uint8_t>
        payload;

    const std::size_t gpuCount =
        (std::min)(
            usage.gpus.size(),
            MAX_GPU_COUNT
            );

    payload.reserve(
        5 + gpuCount * 3
    );

    // CPU
    AppendUInt16LE(
        payload,
        ScalePercentage(
            usage.cpu
        )
    );

    // Memory
    AppendUInt16LE(
        payload,
        ScalePercentage(
            usage.memory
        )
    );

    // GPU count: 0, 1 or 2
    payload.push_back(
        static_cast<std::uint8_t>(
            gpuCount
            )
    );

    for (std::size_t i = 0;
        i < gpuCount;
        ++i)
    {
        // GPU ID
        payload.push_back(
            static_cast<std::uint8_t>(
                i
                )
        );

        // GPU usage
        AppendUInt16LE(
            payload,
            ScalePercentage(
                usage.gpus[i]
                .usagePercent
            )
        );
    }

    return payload;
}

// ============================================================================
// Hardware information
// ============================================================================

std::vector<std::uint8_t>
HardwarePacketEncoder::BuildInfoPayload(
    const HardwareUsage& usage
)
{
    std::vector<std::uint8_t>
        payload;

    const std::size_t gpuCount =
        (std::min)(
            usage.gpus.size(),
            MAX_GPU_COUNT
            );

    // GPU count: 0, 1 or 2
    payload.push_back(
        static_cast<std::uint8_t>(
            gpuCount
            )
    );

    for (std::size_t i = 0;
        i < gpuCount;
        ++i)
    {
        std::string name =
            WideToUtf8(
                usage.gpus[i].name
            );

        name =
            TruncateUtf8(
                name,
                MAX_GPU_NAME_UTF8_LENGTH
            );

        // GPU ID
        payload.push_back(
            static_cast<std::uint8_t>(
                i
                )
        );

        // UTF-8 name length
        AppendUInt16LE(
            payload,
            static_cast<std::uint16_t>(
                name.size()
                )
        );

        // UTF-8 name
        payload.insert(
            payload.end(),
            name.begin(),
            name.end()
        );
    }

    return payload;
}
