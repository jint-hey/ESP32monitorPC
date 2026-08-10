#ifndef CODEX_QUOTA_PACKET_ENCODER_H
#define CODEX_QUOTA_PACKET_ENCODER_H

#include "CodexQuota.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace CodexQuotaProtocol
{
    constexpr std::uint8_t PAYLOAD_VERSION = 1;
    constexpr std::size_t PAYLOAD_SIZE = 43;
}

class CodexQuotaPacketEncoder
{
public:
    static std::vector<std::uint8_t> BuildPayload(
        const CodexQuotaSnapshot& snapshot
    );

private:
    static void WriteUInt16Le(
        std::vector<std::uint8_t>& payload,
        std::size_t offset,
        std::uint16_t value
    );

    static void WriteUInt32Le(
        std::vector<std::uint8_t>& payload,
        std::size_t offset,
        std::uint32_t value
    );

    static void WriteUInt64Le(
        std::vector<std::uint8_t>& payload,
        std::size_t offset,
        std::uint64_t value
    );

    static void WriteWindow(
        std::vector<std::uint8_t>& payload,
        std::size_t offset,
        const CodexQuotaWindow& window
    );
};

#endif // CODEX_QUOTA_PACKET_ENCODER_H
