#ifndef CODEX_QUOTA_H
#define CODEX_QUOTA_H

#include <cstdint>
#include <string>

enum class CodexQuotaStatus : std::uint8_t
{
    Unavailable = 0,
    Valid = 1,
    AuthRequired = 2,
    CollectorError = 3
};

struct CodexQuotaWindow
{
    bool valid = false;
    std::uint16_t usedPercentX100 = 0;
    std::uint16_t remainingPercentX100 = 0;
    std::uint32_t windowDurationMinutes = 0;
    std::uint64_t resetsAtUnixSeconds = 0;
};

struct CodexQuotaSnapshot
{
    CodexQuotaStatus status = CodexQuotaStatus::Unavailable;
    bool rateLimitReached = false;
    // A stale snapshot contains the last successfully collected quota while
    // the collector is starting, retrying, or temporarily unavailable.
    bool stale = false;
    std::uint64_t collectedAtUnixSeconds = 0;
    CodexQuotaWindow quota;
    std::string bucketId;
    std::string windowSource;
    std::uint64_t sequence = 0;
};

#endif // CODEX_QUOTA_H
