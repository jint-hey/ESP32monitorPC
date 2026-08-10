#include "CodexQuotaJsonParser.h"
#include "CodexQuotaPacketEncoder.h"
#include "PacketProtocol.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void Check(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }

    std::uint16_t ReadUInt16Le(
        const std::vector<std::uint8_t>& data,
        const std::size_t offset
    )
    {
        return static_cast<std::uint16_t>(
            data[offset] |
            (static_cast<std::uint16_t>(data[offset + 1]) << 8U)
        );
    }

    std::uint32_t ReadUInt32Le(
        const std::vector<std::uint8_t>& data,
        const std::size_t offset
    )
    {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index)
        {
            value |= static_cast<std::uint32_t>(data[offset + index]) << (index * 8U);
        }
        return value;
    }

    std::uint64_t ReadUInt64Le(
        const std::vector<std::uint8_t>& data,
        const std::size_t offset
    )
    {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index)
        {
            value |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8U);
        }
        return value;
    }

    void TestTwoWindows()
    {
        const auto result = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"result":{"rateLimits":{"primary":{"usedPercent":25.125,"windowDurationMins":15,"resetsAt":1730947200},"secondary":{"usedPercent":42,"windowDurationMins":10080,"resetsAt":1731552000},"rateLimitReachedType":null}}})"
        );

        Check(result.type == CodexAppServerMessageType::Quota, "two-window response parses");
        Check(result.quota.primary.valid, "primary window is valid");
        Check(result.quota.primary.usedPercentX100 == 2513, "fractional percentage rounds");
        Check(result.quota.primary.remainingPercentX100 == 7487, "primary remaining percentage");
        Check(result.quota.secondary.valid, "secondary window is valid");
        Check(result.quota.secondary.windowDurationMinutes == 10080, "secondary duration");
        Check(result.quota.secondary.resetsAtUnixSeconds == 1731552000ULL, "secondary reset time");
    }

    void TestNullableAndInvalidWindows()
    {
        const auto nullable = CodexQuotaJsonParser::ParseLine(
            R"({"method":"account/rateLimits/updated","params":{"rateLimits":{"primary":{"usedPercent":0,"windowDurationMins":300,"resetsAt":1730947200},"secondary":null,"rateLimitReachedType":null}}})"
        );
        Check(nullable.type == CodexAppServerMessageType::Quota, "notification parses");
        Check(nullable.quota.primary.valid, "notification primary window valid");
        Check(!nullable.quota.secondary.valid, "null secondary window invalid flag");
        Check(nullable.secondaryPresent, "explicit null secondary is present");
        Check(nullable.quota.primary.remainingPercentX100 == 10000, "zero used means full remaining");

        const auto partial = CodexQuotaJsonParser::ParseLine(
            R"({"method":"account/rateLimits/updated","params":{"rateLimits":{"primary":{"usedPercent":12,"windowDurationMins":300,"resetsAt":1730947200}}}})"
        );
        Check(partial.type == CodexAppServerMessageType::Quota, "partial notification parses");
        Check(partial.primaryPresent, "partial notification marks primary present");
        Check(!partial.secondaryPresent, "partial notification leaves secondary absent");

        const auto missing = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"result":{"rateLimits":{"primary":{"usedPercent":10,"resetsAt":1},"secondary":null}}})"
        );
        Check(missing.type == CodexAppServerMessageType::Error, "missing duration rejected");

        const auto malformed = CodexQuotaJsonParser::ParseLine("{not-json}");
        Check(malformed.type == CodexAppServerMessageType::Error, "malformed JSON rejected");

        std::string oversized(1024U * 1024U + 1U, ' ');
        const auto tooLong = CodexQuotaJsonParser::ParseLine(oversized);
        Check(tooLong.type == CodexAppServerMessageType::Error, "oversized JSON rejected");
    }

    void TestAuthenticationMessages()
    {
        const auto authenticated = CodexQuotaJsonParser::ParseLine(
            R"({"id":2,"result":{"account":{"type":"chatgpt"},"requiresOpenaiAuth":true}})"
        );
        Check(
            authenticated.type == CodexAppServerMessageType::AccountAuthenticated,
            "ChatGPT account accepted"
        );

        const auto apiKey = CodexQuotaJsonParser::ParseLine(
            R"({"id":2,"result":{"account":{"type":"apiKey"},"requiresOpenaiAuth":true}})"
        );
        Check(apiKey.type == CodexAppServerMessageType::AuthRequired, "API key is not ChatGPT quota auth");
    }

    void TestPayloadAndPacketCrc()
    {
        CodexQuotaSnapshot snapshot;
        snapshot.status = CodexQuotaStatus::Valid;
        snapshot.rateLimitReached = true;
        snapshot.collectedAtUnixSeconds = 0x0102030405060708ULL;
        snapshot.primary = {
            true,
            0,
            10000,
            300,
            0x1112131415161718ULL
        };
        snapshot.secondary = {
            true,
            10000,
            0,
            10080,
            0x2122232425262728ULL
        };

        const std::vector<std::uint8_t> payload =
            CodexQuotaPacketEncoder::BuildPayload(snapshot);
        Check(payload.size() == 43, "payload is exactly 43 bytes");
        Check(payload[0] == 1, "payload schema version");
        Check(payload[1] == static_cast<std::uint8_t>(CodexQuotaStatus::Valid), "payload status");
        Check(payload[2] == 0x07, "payload flags");
        Check(ReadUInt64Le(payload, 3) == snapshot.collectedAtUnixSeconds, "collection timestamp encoding");
        Check(ReadUInt16Le(payload, 11) == 0, "primary used encoding");
        Check(ReadUInt16Le(payload, 13) == 10000, "primary remaining encoding");
        Check(ReadUInt32Le(payload, 15) == 300, "primary duration encoding");
        Check(ReadUInt64Le(payload, 19) == snapshot.primary.resetsAtUnixSeconds, "primary reset encoding");
        Check(ReadUInt16Le(payload, 27) == 10000, "secondary used encoding");
        Check(ReadUInt64Le(payload, 35) == snapshot.secondary.resetsAtUnixSeconds, "secondary reset encoding");

        Packet packet;
        packet.type = PacketType::CodexQuota;
        packet.sequence = 0x1234;
        packet.payload = payload;
        std::vector<std::uint8_t> raw;
        Check(PacketEncoder::Encode(packet, raw), "packet encoding succeeds");
        Check(raw.size() == PacketProtocol::PACKET_OVERHEAD + payload.size(), "packet total size");

        const std::uint16_t encodedCrc = ReadUInt16Le(raw, raw.size() - 2);
        const std::uint16_t calculatedCrc = PacketEncoder::CalculateCRC16(
            raw.data() + 2,
            raw.size() - 4
        );
        Check(encodedCrc == calculatedCrc, "packet CRC covers version through payload");
    }
}

int main()
{
    TestTwoWindows();
    TestNullableAndInvalidWindows();
    TestAuthenticationMessages();
    TestPayloadAndPacketCrc();

    if (failures == 0)
    {
        std::cout << "All Codex quota tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
