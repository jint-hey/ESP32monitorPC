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

    void TestMultiBucketSelectsLongestCodexWindow()
    {
        const auto result = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"result":{"rateLimits":{"limitId":"codex","primary":{"usedPercent":99,"windowDurationMins":5,"resetsAt":1},"secondary":null},"rateLimitsByLimitId":{"codex_other":{"limitId":"codex_other","primary":{"usedPercent":88,"windowDurationMins":20000,"resetsAt":2}},"codex":{"limitId":"codex","primary":{"usedPercent":25.125,"windowDurationMins":300,"resetsAt":1730947200},"secondary":{"usedPercent":18,"windowDurationMins":10080,"resetsAt":1786899540},"rateLimitReachedType":null}}}})"
        );

        Check(result.type == CodexAppServerMessageType::Quota, "multi-bucket response parses");
        Check(result.quotaPresent, "selected quota is present");
        Check(result.quota.bucketId == "codex", "codex bucket selected");
        Check(result.quota.windowSource == "secondary", "longest window selected");
        Check(result.quota.quota.windowDurationMinutes == 10080, "weekly duration selected");
        Check(result.quota.quota.usedPercentX100 == 1800, "weekly percentage selected");
        Check(result.quota.quota.remainingPercentX100 == 8200, "weekly remaining calculated");
        Check(result.quota.quota.resetsAtUnixSeconds == 1786899540ULL, "weekly reset selected");
    }

    void TestFallbackAndLimitIdLookup()
    {
        const auto fallback = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"result":{"rateLimits":{"limitId":"codex","primary":{"usedPercent":25.125,"windowDurationMins":10080,"resetsAt":1730947200},"secondary":null,"rateLimitReachedType":null}}})"
        );
        Check(fallback.type == CodexAppServerMessageType::Quota, "legacy single-bucket view parses");
        Check(fallback.quota.quota.usedPercentX100 == 2513, "fractional percentage rounds");
        Check(fallback.quota.quota.remainingPercentX100 == 7487, "remaining percentage calculated");

        const auto lookup = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"result":{"rateLimitsByLimitId":{"renamed":{"limitId":"codex","primary":{"usedPercent":40,"windowDurationMins":10080,"resetsAt":3},"secondary":null}}}})"
        );
        Check(lookup.type == CodexAppServerMessageType::Quota, "limitId lookup parses");
        Check(lookup.quota.quota.usedPercentX100 == 4000, "limitId lookup uses codex bucket");

        const auto unrelated = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"result":{"rateLimitsByLimitId":{"codex_other":{"limitId":"codex_other","primary":{"usedPercent":1,"windowDurationMins":10080,"resetsAt":4}}}}})"
        );
        Check(unrelated.type == CodexAppServerMessageType::Error, "unrelated bucket is rejected");
    }

    void TestNotificationsAndInvalidInput()
    {
        const auto partial = CodexQuotaJsonParser::ParseLine(
            R"({"method":"account/rateLimits/updated","params":{"rateLimits":{"limitId":"codex","primary":{"usedPercent":12,"windowDurationMins":300,"resetsAt":1730947200}}}})"
        );
        Check(partial.type == CodexAppServerMessageType::Quota, "partial notification parses");
        Check(partial.partialUpdate, "notification is marked partial");
        Check(partial.quotaPresent, "notification window present");
        Check(partial.quota.windowSource == "primary", "notification source retained");

        const auto unrelated = CodexQuotaJsonParser::ParseLine(
            R"({"method":"account/rateLimits/updated","params":{"rateLimits":{"limitId":"codex_other","primary":{"usedPercent":12,"windowDurationMins":300,"resetsAt":1}}}})"
        );
        Check(unrelated.type == CodexAppServerMessageType::Ignored, "unrelated notification ignored");

        const auto missing = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"result":{"rateLimits":{"limitId":"codex","primary":{"usedPercent":10,"resetsAt":1},"secondary":null}}})"
        );
        Check(missing.type == CodexAppServerMessageType::Error, "missing duration rejected");
        Check(CodexQuotaJsonParser::ParseLine("{not-json}").type ==
              CodexAppServerMessageType::Error, "malformed JSON rejected");

        const std::string oversized(1024U * 1024U + 1U, ' ');
        Check(CodexQuotaJsonParser::ParseLine(oversized).type ==
              CodexAppServerMessageType::Error, "oversized JSON rejected");
    }

    void TestAuthenticationMessages()
    {
        const auto authenticated = CodexQuotaJsonParser::ParseLine(
            R"({"id":2,"result":{"account":{"type":"chatgpt"},"requiresOpenaiAuth":true}})"
        );
        Check(authenticated.type == CodexAppServerMessageType::AccountAuthenticated,
              "ChatGPT account accepted");

        const auto apiKey = CodexQuotaJsonParser::ParseLine(
            R"({"id":2,"result":{"account":{"type":"apiKey"},"requiresOpenaiAuth":true}})"
        );
        Check(apiKey.type == CodexAppServerMessageType::AuthRequired,
              "API key is not ChatGPT quota auth");
    }

    void TestUnrelatedRequestIdsAreIgnored()
    {
        const auto serverRequest = CodexQuotaJsonParser::ParseLine(
            R"({"id":99,"method":"some/future/serverRequest","params":{}})"
        );
        Check(serverRequest.type == CodexAppServerMessageType::Ignored,
              "unrelated server request is ignored");

        const auto knownError = CodexQuotaJsonParser::ParseLine(
            R"({"id":3,"error":{"code":-32001,"message":"retry later"}})"
        );
        Check(knownError.type == CodexAppServerMessageType::Error,
              "known quota request error is reported");
        Check(knownError.requestId == 3,
              "known request error identifies the failed request");
    }

    void TestV2PayloadAndPacketCrc()
    {
        CodexQuotaSnapshot snapshot;
        snapshot.status = CodexQuotaStatus::Valid;
        snapshot.rateLimitReached = true;
        snapshot.collectedAtUnixSeconds = 0x0102030405060708ULL;
        snapshot.quota = {
            true,
            1800,
            8200,
            10080,
            0x1112131415161718ULL
        };

        const std::vector<std::uint8_t> payload =
            CodexQuotaPacketEncoder::BuildPayload(snapshot);
        Check(payload.size() == 27, "v2 payload is exactly 27 bytes");
        Check(payload[0] == 2, "payload schema version is v2");
        Check(payload[1] == static_cast<std::uint8_t>(CodexQuotaStatus::Valid),
              "payload status");
        Check(payload[2] == 0x03, "v2 payload flags");
        Check(ReadUInt64Le(payload, 3) == snapshot.collectedAtUnixSeconds,
              "collection timestamp encoding");
        Check(ReadUInt16Le(payload, 11) == 1800, "used percentage encoding");
        Check(ReadUInt16Le(payload, 13) == 8200, "remaining percentage encoding");
        Check(ReadUInt32Le(payload, 15) == 10080, "window duration encoding");
        Check(ReadUInt64Le(payload, 19) == snapshot.quota.resetsAtUnixSeconds,
              "reset timestamp encoding");

        Packet packet;
        packet.type = PacketType::CodexQuota;
        packet.sequence = 0x1234;
        packet.payload = payload;
        std::vector<std::uint8_t> raw;
        Check(PacketEncoder::Encode(packet, raw), "packet encoding succeeds");
        Check(raw.size() == PacketProtocol::PACKET_OVERHEAD + payload.size(),
              "packet total size");
        const std::uint16_t encodedCrc = ReadUInt16Le(raw, raw.size() - 2);
        const std::uint16_t calculatedCrc = PacketEncoder::CalculateCRC16(
            raw.data() + 2,
            raw.size() - 4
        );
        Check(encodedCrc == calculatedCrc, "packet CRC covers v2 payload");

        snapshot.stale = true;
        const std::vector<std::uint8_t> stalePayload =
            CodexQuotaPacketEncoder::BuildPayload(snapshot);
        Check(stalePayload[2] == 0x07,
              "stale v2 payload sets reserved flags bit 2");
    }
}

int main()
{
    TestMultiBucketSelectsLongestCodexWindow();
    TestFallbackAndLimitIdLookup();
    TestNotificationsAndInvalidInput();
    TestAuthenticationMessages();
    TestUnrelatedRequestIdsAreIgnored();
    TestV2PayloadAndPacketCrc();

    if (failures == 0)
    {
        std::cout << "All Codex quota tests passed.\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
