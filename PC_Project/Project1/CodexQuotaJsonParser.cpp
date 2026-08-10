#include "CodexQuotaJsonParser.h"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace
{
    using Json = nlohmann::json;

    std::uint64_t CurrentUnixSeconds()
    {
        const auto now = std::chrono::system_clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()
            ).count()
        );
    }

    bool TryReadUnsigned(
        const Json& object,
        const char* name,
        std::uint64_t maximum,
        std::uint64_t& value
    )
    {
        const auto iterator = object.find(name);
        if (iterator == object.end() || !iterator->is_number())
        {
            return false;
        }

        if (iterator->is_number_unsigned())
        {
            const std::uint64_t number = iterator->get<std::uint64_t>();
            if (number > maximum)
            {
                return false;
            }
            value = number;
            return true;
        }

        if (iterator->is_number_integer())
        {
            const std::int64_t number = iterator->get<std::int64_t>();
            if (number < 0 || static_cast<std::uint64_t>(number) > maximum)
            {
                return false;
            }
            value = static_cast<std::uint64_t>(number);
            return true;
        }

        const double number = iterator->get<double>();
        if (!std::isfinite(number) ||
            number < 0.0 ||
            number > static_cast<double>(maximum))
        {
            return false;
        }

        value = static_cast<std::uint64_t>(number);
        return true;
    }

    bool TryParseWindow(
        const Json& value,
        CodexQuotaWindow& window
    )
    {
        if (value.is_null())
        {
            window = {};
            return true;
        }

        if (!value.is_object())
        {
            return false;
        }

        const auto usedIterator = value.find("usedPercent");
        if (usedIterator == value.end() || !usedIterator->is_number())
        {
            return false;
        }

        const double usedPercent = usedIterator->get<double>();
        if (!std::isfinite(usedPercent))
        {
            return false;
        }

        std::uint64_t duration = 0;
        std::uint64_t resetsAt = 0;
        if (!TryReadUnsigned(
                value,
                "windowDurationMins",
                std::numeric_limits<std::uint32_t>::max(),
                duration) ||
            !TryReadUnsigned(
                value,
                "resetsAt",
                std::numeric_limits<std::uint64_t>::max(),
                resetsAt))
        {
            return false;
        }

        const long long scaled = std::llround(
            std::clamp(usedPercent, 0.0, 100.0) * 100.0
        );

        window.valid = true;
        window.usedPercentX100 = static_cast<std::uint16_t>(scaled);
        window.remainingPercentX100 = static_cast<std::uint16_t>(10000 - scaled);
        window.windowDurationMinutes = static_cast<std::uint32_t>(duration);
        window.resetsAtUnixSeconds = resetsAt;
        return true;
    }

    CodexAppServerMessage ParseQuotaObject(
        const Json& rateLimits,
        const bool allowPartial
    )
    {
        CodexAppServerMessage message;
        message.type = CodexAppServerMessageType::Error;

        if (!rateLimits.is_object())
        {
            message.error = "rateLimits is not an object";
            return message;
        }

        const auto primary = rateLimits.find("primary");
        const auto secondary = rateLimits.find("secondary");
        message.primaryPresent = primary != rateLimits.end();
        message.secondaryPresent = secondary != rateLimits.end();
        if (!allowPartial && (!message.primaryPresent || !message.secondaryPresent))
        {
            message.error = "rateLimits window fields are missing";
            return message;
        }

        if ((message.primaryPresent && !TryParseWindow(*primary, message.quota.primary)) ||
            (message.secondaryPresent && !TryParseWindow(*secondary, message.quota.secondary)) ||
            (!allowPartial &&
             !message.quota.primary.valid &&
             !message.quota.secondary.valid))
        {
            message.error = "rateLimits window fields are invalid";
            return message;
        }

        const auto reached = rateLimits.find("rateLimitReachedType");
        message.rateLimitReachedPresent = reached != rateLimits.end();
        message.quota.rateLimitReached =
            message.rateLimitReachedPresent && !reached->is_null();
        message.quota.status = CodexQuotaStatus::Valid;
        message.quota.collectedAtUnixSeconds = CurrentUnixSeconds();
        message.type = CodexAppServerMessageType::Quota;
        message.error.clear();
        return message;
    }

    bool IsAuthenticatedAccountType(const std::string& type)
    {
        return type == "chatgpt" ||
               type == "chatgptAuthTokens" ||
               type == "agentIdentity" ||
               type == "personalAccessToken";
    }

    std::string ExtractError(const Json& message)
    {
        const auto error = message.find("error");
        if (error == message.end())
        {
            return "Codex App Server returned an unknown error";
        }
        if (error->is_string())
        {
            return error->get<std::string>();
        }
        if (error->is_object())
        {
            const auto text = error->find("message");
            if (text != error->end() && text->is_string())
            {
                return text->get<std::string>();
            }
        }
        return error->dump();
    }
}

CodexAppServerMessage CodexQuotaJsonParser::ParseLine(
    const std::string_view line
)
{
    CodexAppServerMessage parsed;

    if (line.size() > 1024U * 1024U)
    {
        parsed.type = CodexAppServerMessageType::Error;
        parsed.error = "App Server JSON line exceeds 1 MiB";
        return parsed;
    }

    try
    {
        const Json message = Json::parse(line.begin(), line.end());
        if (!message.is_object())
        {
            parsed.type = CodexAppServerMessageType::Error;
            parsed.error = "App Server message is not a JSON object";
            return parsed;
        }

        const auto method = message.find("method");
        if (method != message.end() && method->is_string())
        {
            const std::string methodName = method->get<std::string>();
            if (methodName == "account/updated")
            {
                const auto params = message.find("params");
                if (params == message.end() || !params->is_object())
                {
                    parsed.type = CodexAppServerMessageType::Error;
                    parsed.error = "account notification params are missing";
                    return parsed;
                }

                const auto authMode = params->find("authMode");
                parsed.type =
                    authMode != params->end() && authMode->is_string() &&
                    IsAuthenticatedAccountType(authMode->get<std::string>())
                    ? CodexAppServerMessageType::AccountAuthenticated
                    : CodexAppServerMessageType::AuthRequired;
                return parsed;
            }

            if (methodName != "account/rateLimits/updated")
            {
                return parsed;
            }

            const auto params = message.find("params");
            if (params == message.end() || !params->is_object())
            {
                parsed.type = CodexAppServerMessageType::Error;
                parsed.error = "rateLimits notification params are missing";
                return parsed;
            }

            const auto limits = params->find("rateLimits");
            if (limits == params->end())
            {
                parsed.type = CodexAppServerMessageType::Error;
                parsed.error = "rateLimits notification data is missing";
                return parsed;
            }
            return ParseQuotaObject(*limits, true);
        }

        const auto id = message.find("id");
        if (id == message.end() || !id->is_number_integer())
        {
            return parsed;
        }

        const int requestId = id->get<int>();
        if (message.contains("error"))
        {
            if (requestId == INITIALIZE_REQUEST_ID ||
                requestId == ACCOUNT_REQUEST_ID ||
                requestId == RATE_LIMITS_REQUEST_ID)
            {
                parsed.type = CodexAppServerMessageType::Error;
                parsed.error = ExtractError(message);
            }
            return parsed;
        }

        const auto result = message.find("result");
        if (result == message.end() || !result->is_object())
        {
            parsed.type = CodexAppServerMessageType::Error;
            parsed.error = "App Server response result is missing";
            return parsed;
        }

        if (requestId == INITIALIZE_REQUEST_ID)
        {
            parsed.type = CodexAppServerMessageType::Initialized;
            return parsed;
        }

        if (requestId == ACCOUNT_REQUEST_ID)
        {
            const auto account = result->find("account");
            if (account == result->end() || account->is_null() || !account->is_object())
            {
                parsed.type = CodexAppServerMessageType::AuthRequired;
                return parsed;
            }

            const auto type = account->find("type");
            parsed.type =
                type != account->end() && type->is_string() &&
                IsAuthenticatedAccountType(type->get<std::string>())
                ? CodexAppServerMessageType::AccountAuthenticated
                : CodexAppServerMessageType::AuthRequired;
            return parsed;
        }

        if (requestId == RATE_LIMITS_REQUEST_ID)
        {
            const auto limits = result->find("rateLimits");
            if (limits == result->end())
            {
                parsed.type = CodexAppServerMessageType::Error;
                parsed.error = "rateLimits response data is missing";
                return parsed;
            }
            return ParseQuotaObject(*limits, false);
        }
    }
    catch (const std::exception& exception)
    {
        parsed.type = CodexAppServerMessageType::Error;
        parsed.error = exception.what();
    }

    return parsed;
}
