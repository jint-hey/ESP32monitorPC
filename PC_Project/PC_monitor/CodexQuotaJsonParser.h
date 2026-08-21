#ifndef CODEX_QUOTA_JSON_PARSER_H
#define CODEX_QUOTA_JSON_PARSER_H

#include "CodexQuota.h"

#include <string>
#include <string_view>

enum class CodexAppServerMessageType
{
    Ignored,
    Initialized,
    AccountAuthenticated,
    AuthRequired,
    Quota,
    Error
};

struct CodexAppServerMessage
{
    CodexAppServerMessageType type = CodexAppServerMessageType::Ignored;
    int requestId = -1;
    CodexQuotaSnapshot quota;
    bool quotaPresent = false;
    bool rateLimitReachedPresent = false;
    bool partialUpdate = false;
    std::string error;
};

class CodexQuotaJsonParser
{
public:
    static constexpr int INITIALIZE_REQUEST_ID = 1;
    static constexpr int ACCOUNT_REQUEST_ID = 2;
    static constexpr int RATE_LIMITS_REQUEST_ID = 3;

    static CodexAppServerMessage ParseLine(std::string_view line);
};

#endif // CODEX_QUOTA_JSON_PARSER_H
