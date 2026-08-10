#pragma once

#include "pc_protocol.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstdint>

enum class CodexQuotaStatus : uint8_t
{
    Unavailable = 0,
    Valid = 1,
    AuthRequired = 2,
    CollectorError = 3
};

struct CodexQuotaWindow
{
    bool valid = false;
    uint16_t usedPercentX100 = 0;
    uint16_t remainingPercentX100 = 0;
    uint32_t windowDurationMinutes = 0;
    uint64_t resetsAtUnixSeconds = 0;
};

struct CodexQuotaSnapshot
{
    CodexQuotaStatus status = CodexQuotaStatus::Unavailable;
    bool rateLimitReached = false;
    uint64_t collectedAtUnixSeconds = 0;
    CodexQuotaWindow quota;
    uint32_t updateCount = 0;
    uint16_t lastPacketSequence = 0;
    bool hasPacket = false;
};

class CodexQuotaStateStore
{
public:
    CodexQuotaStateStore();

    CodexQuotaStateStore(const CodexQuotaStateStore&) = delete;
    CodexQuotaStateStore& operator=(const CodexQuotaStateStore&) = delete;

    bool ApplyPacket(const pc_protocol::Packet& packet);
    CodexQuotaSnapshot Copy() const;

private:
    mutable SemaphoreHandle_t mutex_ = nullptr;
    CodexQuotaSnapshot state_{};
};
