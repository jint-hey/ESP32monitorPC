#include "CodexQuotaMonitor.h"

#include "CodexQuotaJsonParser.h"
#include "ConsoleLogger.h"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* INITIALIZE_REQUEST =
        R"({"method":"initialize","id":1,"params":{"clientInfo":{"name":"pc_hardware_monitor","title":"PC Hardware Monitor","version":"1.0.0"}}})";
    constexpr const char* INITIALIZED_NOTIFICATION =
        R"({"method":"initialized","params":{}})";
    constexpr const char* ACCOUNT_REQUEST =
        R"({"method":"account/read","id":2,"params":{"refreshToken":false}})";
    constexpr const char* RATE_LIMITS_REQUEST =
        R"({"method":"account/rateLimits/read","id":3})";

    constexpr auto IO_POLL_INTERVAL = std::chrono::milliseconds(100);
    constexpr auto HANDSHAKE_TIMEOUT = std::chrono::seconds(15);
    constexpr auto REQUEST_TIMEOUT = std::chrono::seconds(10);
    constexpr auto REFRESH_INTERVAL = std::chrono::seconds(60);
    constexpr std::array<std::chrono::seconds, 3> REQUEST_RETRY_DELAYS = {
        std::chrono::seconds(2),
        std::chrono::seconds(5),
        std::chrono::seconds(10)
    };
    constexpr std::array<std::chrono::seconds, 3> RESTART_DELAYS = {
        std::chrono::seconds(5),
        std::chrono::seconds(15),
        std::chrono::seconds(60)
    };
    constexpr std::uint64_t CACHE_MAX_AGE_SECONDS = 14ULL * 24ULL * 60ULL * 60ULL;
    constexpr int CACHE_VERSION = 1;
    constexpr std::uint32_t COLLECTOR_ERROR_THRESHOLD = 3;

    using Json = nlohmann::json;

    void CloseHandleIfValid(HANDLE& handle)
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
        handle = nullptr;
    }

    bool IsRegularFile(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    }

    std::wstring GetEnvironmentValue(const wchar_t* name)
    {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0)
        {
            return {};
        }

        std::wstring value(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(
            name,
            value.data(),
            static_cast<DWORD>(value.size())
        );
        if (written == 0 || written >= value.size())
        {
            return {};
        }
        value.resize(written);
        return value;
    }

    std::filesystem::path GetModuleDirectory()
    {
        std::vector<wchar_t> buffer(512);
        while (buffer.size() <= 32768)
        {
            const DWORD length = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size())
            );
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size() - 1)
            {
                return std::filesystem::path(
                    std::wstring(buffer.data(), length)
                ).parent_path();
            }
            buffer.resize(buffer.size() * 2);
        }
        return {};
    }

    std::filesystem::path GetQuotaCachePath()
    {
        const std::wstring localAppData = GetEnvironmentValue(L"LOCALAPPDATA");
        if (localAppData.empty())
        {
            return {};
        }
        return std::filesystem::path(localAppData) /
               L"PC Hardware Monitor" /
               L"codex_quota_cache.json";
    }

    bool IsProcessRunning(HANDLE process)
    {
        return process != nullptr &&
               WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
}

CodexQuotaMonitor::CodexQuotaMonitor() = default;

CodexQuotaMonitor::~CodexQuotaMonitor()
{
    Stop();
}

void CodexQuotaMonitor::SetStatusCallback(StatusCallback callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    statusCallback_ = std::move(callback);
}

bool CodexQuotaMonitor::Start(ConsoleLogger* logger)
{
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        if (running_)
        {
            return true;
        }

        logger_ = logger;
        stopRequested_ = false;
        running_ = true;
    }

    // Publish a recent last-known value before starting App Server so a cold
    // PC/ESP32 start does not leave the OLED blank while the network warms up.
    LoadCachedSnapshot();

    try
    {
        workerThread_ = std::thread(&CodexQuotaMonitor::WorkerLoop, this);
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        running_ = false;
        logger_ = nullptr;
        return false;
    }
    return true;
}

void CodexQuotaMonitor::Stop()
{
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        if (!running_)
        {
            return;
        }
        stopRequested_ = true;
    }
    condition_.notify_all();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    std::lock_guard<std::mutex> lock(controlMutex_);
    running_ = false;
    stopRequested_ = false;
    logger_ = nullptr;
}

bool CodexQuotaMonitor::IsRunning() const
{
    return running_.load();
}

CodexQuotaSnapshot CodexQuotaMonitor::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

void CodexQuotaMonitor::WorkerLoop()
{
    std::size_t restartIndex = 0;

    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            if (stopRequested_)
            {
                break;
            }
        }

        const std::wstring executablePath = FindCodexExecutable();
        bool receivedValidQuota = false;

        if (executablePath.empty())
        {
            PublishStatus(CodexQuotaStatus::Unavailable, true);
            LogWarning(L"Codex executable was not found. Quota monitoring is unavailable.");
        }
        else if (!StartAppServer(executablePath))
        {
            ReportCollectorFailure();
            LogError(L"Unable to start Codex App Server.");
        }
        else
        {
            LogInfo(L"Codex App Server started.");
            receivedValidQuota = RunAppServerSession();
            CloseAppServer(true);

            std::lock_guard<std::mutex> lock(controlMutex_);
            if (stopRequested_)
            {
                break;
            }

            ReportCollectorFailure();
            LogWarning(L"Codex App Server stopped unexpectedly.");
        }

        if (receivedValidQuota)
        {
            restartIndex = 0;
        }

        const auto delay = RESTART_DELAYS[std::min(
            restartIndex,
            RESTART_DELAYS.size() - 1
        )];
        if (restartIndex + 1 < RESTART_DELAYS.size())
        {
            ++restartIndex;
        }

        LogInfo(
            L"Codex quota monitor will retry in " +
            std::to_wstring(delay.count()) +
            L" seconds."
        );

        std::unique_lock<std::mutex> lock(controlMutex_);
        condition_.wait_for(
            lock,
            delay,
            [this]() { return stopRequested_; }
        );
        if (stopRequested_)
        {
            break;
        }
    }

    CloseAppServer(true);
    running_ = false;
}

bool CodexQuotaMonitor::StartAppServer(const std::wstring& executablePath)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE childStandardInputRead = nullptr;
    HANDLE childStandardOutputWrite = nullptr;
    HANDLE childStandardErrorWrite = nullptr;

    if (!CreatePipe(&childStandardInputRead, &standardInputWrite_, &security, 0) ||
        !CreatePipe(&standardOutputRead_, &childStandardOutputWrite, &security, 0) ||
        !CreatePipe(&standardErrorRead_, &childStandardErrorWrite, &security, 0))
    {
        CloseHandleIfValid(childStandardInputRead);
        CloseHandleIfValid(childStandardOutputWrite);
        CloseHandleIfValid(childStandardErrorWrite);
        CloseAppServer(false);
        return false;
    }

    if (!SetHandleInformation(standardInputWrite_, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(standardOutputRead_, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(standardErrorRead_, HANDLE_FLAG_INHERIT, 0))
    {
        CloseHandleIfValid(childStandardInputRead);
        CloseHandleIfValid(childStandardOutputWrite);
        CloseHandleIfValid(childStandardErrorWrite);
        CloseAppServer(false);
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childStandardInputRead;
    startup.hStdOutput = childStandardOutputWrite;
    startup.hStdError = childStandardErrorWrite;

    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executablePath + L"\" app-server --listen stdio://";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(
        executablePath.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr,
        nullptr,
        &startup,
        &process
    );

    CloseHandleIfValid(childStandardInputRead);
    CloseHandleIfValid(childStandardOutputWrite);
    CloseHandleIfValid(childStandardErrorWrite);

    if (!created)
    {
        CloseAppServer(false);
        return false;
    }

    processHandle_ = process.hProcess;
    jobHandle_ = CreateJobObjectW(nullptr, nullptr);
    if (jobHandle_ != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
        information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                jobHandle_,
                JobObjectExtendedLimitInformation,
                &information,
                sizeof(information)) ||
            !AssignProcessToJobObject(jobHandle_, processHandle_))
        {
            CloseHandleIfValid(jobHandle_);
        }
    }

    const bool resumed = ResumeThread(process.hThread) != static_cast<DWORD>(-1);
    CloseHandle(process.hThread);
    if (!resumed)
    {
        CloseAppServer(true);
        return false;
    }
    return true;
}

bool CodexQuotaMonitor::RunAppServerSession()
{
    using Clock = std::chrono::steady_clock;

    if (!WriteJsonLine(INITIALIZE_REQUEST))
    {
        return false;
    }
    if (logger_ != nullptr)
    {
        logger_->LogCodexQuery(L"initialize", L"App Server startup");
    }

    bool authenticated = false;
    bool receivedQuota = false;
    bool initialized = false;
    bool accountRequestPending = false;
    bool quotaRequestPending = false;
    std::size_t accountRetryIndex = 0;
    std::size_t quotaRetryIndex = 0;
    std::string outputBuffer;
    std::string errorBuffer;
    const auto handshakeDeadline = Clock::now() + HANDSHAKE_TIMEOUT;
    auto accountRequestDeadline = Clock::time_point::max();
    auto quotaRequestDeadline = Clock::time_point::max();
    auto nextRefresh = Clock::time_point::max();
    auto nextAccountCheck = Clock::time_point::max();

    const auto retryDelay = [](const std::size_t index)
    {
        return REQUEST_RETRY_DELAYS[std::min(
            index,
            REQUEST_RETRY_DELAYS.size() - 1
        )];
    };

    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            if (stopRequested_)
            {
                return receivedQuota;
            }
        }

        if (!IsProcessRunning(processHandle_))
        {
            return receivedQuota;
        }

        const bool wasInitialized = initialized;
        bool accountResponseReceived = false;
        bool quotaResponseReceived = false;
        bool accountRequestFailed = false;
        bool quotaRequestFailed = false;
        if (!DrainOutput(
                outputBuffer,
                initialized,
                authenticated,
                receivedQuota,
                accountResponseReceived,
                quotaResponseReceived,
                accountRequestFailed,
                quotaRequestFailed))
        {
            return receivedQuota;
        }
        DrainErrorOutput(errorBuffer);

        const auto now = Clock::now();

        if (initialized && !wasInitialized)
        {
            // HandleJsonLine sent the initial account/read immediately after
            // acknowledging initialization.
            accountRequestPending = true;
            accountRequestDeadline = now + REQUEST_TIMEOUT;
        }

        if (accountResponseReceived)
        {
            accountRequestPending = false;
            accountRequestDeadline = Clock::time_point::max();

            if (accountRequestFailed)
            {
                authenticated = false;
                nextAccountCheck = now + retryDelay(accountRetryIndex++);
            }
            else if (authenticated)
            {
                accountRetryIndex = 0;
                nextAccountCheck = Clock::time_point::max();
                if (!WriteJsonLine(RATE_LIMITS_REQUEST))
                {
                    return receivedQuota;
                }
                if (logger_ != nullptr)
                {
                    logger_->LogCodexQuery(
                        L"account/rateLimits/read",
                        L"Initial quota query"
                    );
                }
                quotaRequestPending = true;
                quotaRequestDeadline = now + REQUEST_TIMEOUT;
                nextRefresh = Clock::time_point::max();
            }
            else
            {
                accountRetryIndex = 0;
                nextAccountCheck = now + REFRESH_INTERVAL;
            }
        }

        if (quotaResponseReceived)
        {
            quotaRequestPending = false;
            quotaRequestDeadline = Clock::time_point::max();
            if (quotaRequestFailed)
            {
                nextRefresh = now + retryDelay(quotaRetryIndex++);
            }
            else
            {
                quotaRetryIndex = 0;
                nextRefresh = now + REFRESH_INTERVAL;
            }
        }

        if (!initialized && now >= handshakeDeadline)
        {
            LogError(L"Codex App Server initialization timed out.");
            return receivedQuota;
        }

        if (accountRequestPending && now >= accountRequestDeadline)
        {
            accountRequestPending = false;
            authenticated = false;
            ReportCollectorFailure();
            LogWarning(L"Codex account/read response timed out; retrying quickly.");
            nextAccountCheck = now + retryDelay(accountRetryIndex++);
        }

        if (quotaRequestPending && now >= quotaRequestDeadline)
        {
            quotaRequestPending = false;
            ReportCollectorFailure();
            LogWarning(L"Codex rate-limit response timed out; retrying quickly.");
            nextRefresh = now + retryDelay(quotaRetryIndex++);
        }

        if (authenticated &&
            !quotaRequestPending &&
            now >= nextRefresh)
        {
            if (!WriteJsonLine(RATE_LIMITS_REQUEST))
            {
                return receivedQuota;
            }
            if (logger_ != nullptr)
            {
                logger_->LogCodexQuery(
                    L"account/rateLimits/read",
                    L"60-second periodic refresh"
                );
            }
            quotaRequestPending = true;
            quotaRequestDeadline = now + REQUEST_TIMEOUT;
            nextRefresh = Clock::time_point::max();
        }

        if (initialized &&
            !authenticated &&
            !accountRequestPending &&
            now >= nextAccountCheck)
        {
            if (!WriteJsonLine(ACCOUNT_REQUEST))
            {
                return receivedQuota;
            }
            if (logger_ != nullptr)
            {
                logger_->LogCodexQuery(
                    L"account/read",
                    L"60-second authentication recheck"
                );
            }
            accountRequestPending = true;
            accountRequestDeadline = now + REQUEST_TIMEOUT;
            nextAccountCheck = Clock::time_point::max();
        }

        std::unique_lock<std::mutex> lock(controlMutex_);
        condition_.wait_for(
            lock,
            IO_POLL_INTERVAL,
            [this]() { return stopRequested_; }
        );
    }
}

bool CodexQuotaMonitor::WriteJsonLine(const char* json)
{
    if (standardInputWrite_ == nullptr)
    {
        return false;
    }

    std::string line(json);
    line.push_back('\n');
    std::size_t offset = 0;
    while (offset < line.size())
    {
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(line.size() - offset);
        if (!WriteFile(
                standardInputWrite_,
                line.data() + offset,
                remaining,
                &written,
                nullptr) ||
            written == 0)
        {
            return false;
        }
        offset += written;
    }
    return true;
}

bool CodexQuotaMonitor::DrainOutput(
    std::string& lineBuffer,
    bool& initialized,
    bool& authenticated,
    bool& receivedQuota,
    bool& accountResponseReceived,
    bool& quotaResponseReceived,
    bool& accountRequestFailed,
    bool& quotaRequestFailed
)
{
    DWORD available = 0;
    if (!PeekNamedPipe(standardOutputRead_, nullptr, 0, nullptr, &available, nullptr))
    {
        return GetLastError() == ERROR_BROKEN_PIPE;
    }

    std::array<char, 4096> buffer{};
    while (available > 0)
    {
        DWORD bytesRead = 0;
        const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(standardOutputRead_, buffer.data(), toRead, &bytesRead, nullptr))
        {
            return false;
        }
        lineBuffer.append(buffer.data(), bytesRead);
        if (lineBuffer.size() > MAX_JSON_LINE_SIZE)
        {
            LogError(L"Codex App Server produced an oversized JSON line.");
            return false;
        }

        std::size_t newline = 0;
        while ((newline = lineBuffer.find('\n')) != std::string::npos)
        {
            std::string line = lineBuffer.substr(0, newline);
            lineBuffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty() &&
                !HandleJsonLine(
                    line,
                    initialized,
                    authenticated,
                    receivedQuota,
                    accountResponseReceived,
                    quotaResponseReceived,
                    accountRequestFailed,
                    quotaRequestFailed))
            {
                return false;
            }
        }

        if (!PeekNamedPipe(standardOutputRead_, nullptr, 0, nullptr, &available, nullptr))
        {
            return GetLastError() == ERROR_BROKEN_PIPE;
        }
    }
    return true;
}

void CodexQuotaMonitor::DrainErrorOutput(std::string& errorBuffer)
{
    DWORD available = 0;
    if (!PeekNamedPipe(standardErrorRead_, nullptr, 0, nullptr, &available, nullptr))
    {
        return;
    }

    std::array<char, 2048> buffer{};
    while (available > 0)
    {
        DWORD bytesRead = 0;
        const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(standardErrorRead_, buffer.data(), toRead, &bytesRead, nullptr))
        {
            return;
        }
        errorBuffer.append(buffer.data(), bytesRead);
        if (errorBuffer.size() > 65536)
        {
            errorBuffer.erase(0, errorBuffer.size() - 65536);
        }

        std::size_t newline = 0;
        while ((newline = errorBuffer.find('\n')) != std::string::npos)
        {
            std::string line = errorBuffer.substr(0, newline);
            errorBuffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                LogWarning(L"Codex App Server: " + Utf8ToWide(line));
            }
        }

        if (!PeekNamedPipe(standardErrorRead_, nullptr, 0, nullptr, &available, nullptr))
        {
            return;
        }
    }
}

bool CodexQuotaMonitor::HandleJsonLine(
    const std::string& line,
    bool& initialized,
    bool& authenticated,
    bool& receivedQuota,
    bool& accountResponseReceived,
    bool& quotaResponseReceived,
    bool& accountRequestFailed,
    bool& quotaRequestFailed
)
{
    const CodexAppServerMessage message = CodexQuotaJsonParser::ParseLine(line);
    switch (message.type)
    {
    case CodexAppServerMessageType::Ignored:
        return true;

    case CodexAppServerMessageType::Initialized:
        if (!WriteJsonLine(INITIALIZED_NOTIFICATION) ||
            !WriteJsonLine(ACCOUNT_REQUEST))
        {
            return false;
        }
        initialized = true;
        if (logger_ != nullptr)
        {
            logger_->LogCodexQuery(L"account/read", L"Initial authentication check");
        }
        return true;

    case CodexAppServerMessageType::AccountAuthenticated:
        authenticated = true;
        accountResponseReceived = true;
        if (logger_ != nullptr)
        {
            logger_->LogCodexAccount(true);
        }
        LogInfo(L"Codex ChatGPT authentication is available.");
        return true;

    case CodexAppServerMessageType::AuthRequired:
        authenticated = false;
        accountResponseReceived = true;
        consecutiveCollectorFailures_ = 0;
        PublishStatus(CodexQuotaStatus::AuthRequired);
        if (logger_ != nullptr)
        {
            logger_->LogCodexAccount(false);
        }
        LogWarning(L"Codex ChatGPT login is required for quota monitoring.");
        return true;

    case CodexAppServerMessageType::Quota:
    {
        CodexQuotaSnapshot quota = message.quota;
        quota.stale = false;
        const CodexQuotaSnapshot previous = GetSnapshot();
        if (!message.quotaPresent)
        {
            quota.quota = previous.quota;
            quota.windowSource = previous.windowSource;
        }
        else if (message.partialUpdate &&
                 previous.quota.valid &&
                 quota.windowSource != previous.windowSource &&
                 quota.quota.windowDurationMinutes <
                     previous.quota.windowDurationMinutes)
        {
            // Notifications can update only the short window. Keep the
            // already-selected longer (weekly) window in that case.
            quota.quota = previous.quota;
            quota.windowSource = previous.windowSource;
        }
        if (!message.rateLimitReachedPresent)
        {
            quota.rateLimitReached = previous.rateLimitReached;
        }
        if (!quota.quota.valid)
        {
            LogWarning(L"Codex quota update did not contain a usable window.");
            return true;
        }
        PublishSnapshot(quota);
        consecutiveCollectorFailures_ = 0;
        SaveCachedSnapshot(GetSnapshot());
        quotaResponseReceived = true;
        if (logger_ != nullptr)
        {
            logger_->LogCodexQuota(GetSnapshot());
        }
        receivedQuota = true;
        LogInfo(L"Codex quota snapshot refreshed.");
        return true;
    }

    case CodexAppServerMessageType::Error:
        LogError(L"Codex App Server protocol error: " + Utf8ToWide(message.error));

        if (message.requestId == CodexQuotaJsonParser::INITIALIZE_REQUEST_ID)
        {
            // The outer session loop reports this fatal connection failure.
            return false;
        }

        ReportCollectorFailure();
        if (message.requestId == CodexQuotaJsonParser::ACCOUNT_REQUEST_ID)
        {
            accountResponseReceived = true;
            accountRequestFailed = true;
        }
        else if (message.requestId == CodexQuotaJsonParser::RATE_LIMITS_REQUEST_ID)
        {
            quotaResponseReceived = true;
            quotaRequestFailed = true;
        }

        // A malformed unrelated line is logged and ignored. If it replaced a
        // pending response, the request deadline will trigger a fast retry.
        return true;
    }

    return true;
}

void CodexQuotaMonitor::CloseAppServer(const bool terminateProcess)
{
    CloseHandleIfValid(standardInputWrite_);

    if (processHandle_ != nullptr && IsProcessRunning(processHandle_))
    {
        if (terminateProcess)
        {
            if (WaitForSingleObject(processHandle_, 1000) == WAIT_TIMEOUT)
            {
                TerminateProcess(processHandle_, 1);
                WaitForSingleObject(processHandle_, 1000);
            }
        }
    }

    CloseHandleIfValid(standardOutputRead_);
    CloseHandleIfValid(standardErrorRead_);
    CloseHandleIfValid(processHandle_);
    CloseHandleIfValid(jobHandle_);
}

void CodexQuotaMonitor::PublishSnapshot(CodexQuotaSnapshot snapshot)
{
    StatusCallback callback;
    bool statusChanged = false;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        statusChanged = !hasPublishedStatus_ || snapshot_.status != snapshot.status;
        snapshot.sequence = ++nextSequence_;
        snapshot_ = snapshot;
        hasPublishedStatus_ = true;
    }

    if (statusChanged)
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback = statusCallback_;
    }
    if (callback)
    {
        callback(snapshot.status);
    }
}

void CodexQuotaMonitor::PublishStatus(
    const CodexQuotaStatus status,
    const bool preserveQuota)
{
    CodexQuotaSnapshot snapshot = preserveQuota
        ? GetSnapshot()
        : CodexQuotaSnapshot{};

    snapshot.status = status;
    snapshot.stale = preserveQuota && snapshot.quota.valid;
    if (!snapshot.quota.valid)
    {
        snapshot.collectedAtUnixSeconds = CurrentUnixSeconds();
    }
    PublishSnapshot(snapshot);
}

void CodexQuotaMonitor::ReportCollectorFailure()
{
    ++consecutiveCollectorFailures_;
    CodexQuotaSnapshot snapshot = GetSnapshot();

    if (snapshot.quota.valid)
    {
        snapshot.stale = true;
        if (consecutiveCollectorFailures_ >= COLLECTOR_ERROR_THRESHOLD)
        {
            snapshot.status = CodexQuotaStatus::CollectorError;
        }
        else
        {
            // A single transient failure should mark the value stale without
            // presenting a disruptive collector error to the user.
            snapshot.status = CodexQuotaStatus::Valid;
        }
        PublishSnapshot(std::move(snapshot));
    }
    else if (consecutiveCollectorFailures_ >= COLLECTOR_ERROR_THRESHOLD)
    {
        PublishStatus(CodexQuotaStatus::CollectorError);
    }
}

void CodexQuotaMonitor::LoadCachedSnapshot()
{
    const std::filesystem::path path = GetQuotaCachePath();
    if (path.empty())
    {
        return;
    }

    try
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return;
        }

        Json cache;
        input >> cache;
        if (!cache.is_object() ||
            cache.at("version").get<int>() != CACHE_VERSION)
        {
            return;
        }

        CodexQuotaSnapshot snapshot;
        snapshot.status = CodexQuotaStatus::Valid;
        snapshot.stale = true;
        snapshot.rateLimitReached = cache.at("rate_limit_reached").get<bool>();
        snapshot.collectedAtUnixSeconds = cache.at("collected_at").get<std::uint64_t>();
        snapshot.quota.valid = true;
        snapshot.quota.usedPercentX100 = cache.at("used_percent_x100").get<std::uint16_t>();
        snapshot.quota.remainingPercentX100 = cache.at("remaining_percent_x100").get<std::uint16_t>();
        snapshot.quota.windowDurationMinutes = cache.at("window_duration_minutes").get<std::uint32_t>();
        snapshot.quota.resetsAtUnixSeconds = cache.at("resets_at").get<std::uint64_t>();
        snapshot.bucketId = cache.value("bucket_id", std::string("codex"));
        snapshot.windowSource = cache.value("window_source", std::string{});

        const std::uint64_t now = CurrentUnixSeconds();
        if (snapshot.collectedAtUnixSeconds == 0 ||
            snapshot.collectedAtUnixSeconds > now ||
            now - snapshot.collectedAtUnixSeconds > CACHE_MAX_AGE_SECONDS ||
            snapshot.quota.usedPercentX100 > 10000 ||
            snapshot.quota.remainingPercentX100 > 10000 ||
            snapshot.quota.windowDurationMinutes == 0)
        {
            return;
        }

        PublishSnapshot(std::move(snapshot));
        LogInfo(L"Loaded the last valid Codex quota cache.");
    }
    catch (const std::exception& exception)
    {
        LogWarning(L"Ignoring invalid Codex quota cache: " +
                   Utf8ToWide(exception.what()));
    }
}

void CodexQuotaMonitor::SaveCachedSnapshot(
    const CodexQuotaSnapshot& snapshot) const
{
    if (!snapshot.quota.valid || snapshot.stale)
    {
        return;
    }

    const std::filesystem::path path = GetQuotaCachePath();
    if (path.empty())
    {
        return;
    }

    try
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            LogWarning(L"Unable to create the Codex quota cache directory.");
            return;
        }

        Json cache = {
            {"version", CACHE_VERSION},
            {"collected_at", snapshot.collectedAtUnixSeconds},
            {"used_percent_x100", snapshot.quota.usedPercentX100},
            {"remaining_percent_x100", snapshot.quota.remainingPercentX100},
            {"window_duration_minutes", snapshot.quota.windowDurationMinutes},
            {"resets_at", snapshot.quota.resetsAtUnixSeconds},
            {"rate_limit_reached", snapshot.rateLimitReached},
            {"bucket_id", snapshot.bucketId},
            {"window_source", snapshot.windowSource}
        };

        std::filesystem::path temporary = path;
        temporary += L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                LogWarning(L"Unable to open the Codex quota cache for writing.");
                return;
            }
            output << cache.dump();
            output.flush();
            if (!output)
            {
                LogWarning(L"Unable to write the Codex quota cache.");
                return;
            }
        }

        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, error);
            LogWarning(L"Unable to replace the Codex quota cache.");
        }
    }
    catch (const std::exception& exception)
    {
        LogWarning(L"Unable to save the Codex quota cache: " +
                   Utf8ToWide(exception.what()));
    }
}

void CodexQuotaMonitor::LogInfo(const std::wstring& message) const
{
    if (logger_ != nullptr)
    {
        logger_->Info(message);
    }
}

void CodexQuotaMonitor::LogWarning(const std::wstring& message) const
{
    if (logger_ != nullptr)
    {
        logger_->Warning(message);
    }
}

void CodexQuotaMonitor::LogError(const std::wstring& message) const
{
    if (logger_ != nullptr)
    {
        logger_->Error(message);
    }
}

std::wstring CodexQuotaMonitor::FindCodexExecutable()
{
    const std::wstring overridePath = GetEnvironmentValue(L"CODEX_EXE_PATH");
    if (!overridePath.empty() && IsRegularFile(overridePath))
    {
        return overridePath;
    }

    const std::filesystem::path adjacent = GetModuleDirectory() / L"codex.exe";
    if (IsRegularFile(adjacent))
    {
        return adjacent.wstring();
    }

    const DWORD searchLength = SearchPathW(nullptr, L"codex.exe", nullptr, 0, nullptr, nullptr);
    if (searchLength > 0)
    {
        std::wstring found(searchLength + 1, L'\0');
        const DWORD written = SearchPathW(
            nullptr,
            L"codex.exe",
            nullptr,
            static_cast<DWORD>(found.size()),
            found.data(),
            nullptr
        );
        if (written > 0 && written < found.size())
        {
            found.resize(written);
            if (IsRegularFile(found))
            {
                return found;
            }
        }
    }

    const std::wstring userProfile = GetEnvironmentValue(L"USERPROFILE");
    if (userProfile.empty())
    {
        return {};
    }

    const std::filesystem::path extensionRoot =
        std::filesystem::path(userProfile) / L".vscode" / L"extensions";
    std::filesystem::path selected;
    std::wstring selectedDirectoryName;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(extensionRoot, error), end;
         !error && iterator != end;
         iterator.increment(error))
    {
        if (!iterator->is_directory(error))
        {
            continue;
        }

        const std::wstring name = iterator->path().filename().wstring();
        if (name.rfind(L"openai.chatgpt-", 0) != 0)
        {
            continue;
        }

        const std::filesystem::path candidate =
            iterator->path() / L"bin" / L"windows-x86_64" / L"codex.exe";
        if (IsRegularFile(candidate) &&
            (selected.empty() || name > selectedDirectoryName))
        {
            selected = candidate;
            selectedDirectoryName = name;
        }
    }
    return selected.wstring();
}

std::wstring CodexQuotaMonitor::Utf8ToWide(const std::string_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (required <= 0)
    {
        return L"<invalid UTF-8>";
    }

    std::wstring result(required, L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        required
    );
    return result;
}

std::uint64_t CodexQuotaMonitor::CurrentUnixSeconds()
{
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()
        ).count()
    );
}
