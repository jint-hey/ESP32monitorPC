#include "CodexQuotaMonitor.h"

#include "CodexQuotaJsonParser.h"
#include "ConsoleLogger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
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
    constexpr auto REFRESH_INTERVAL = std::chrono::seconds(60);
    constexpr std::array<std::chrono::seconds, 3> RESTART_DELAYS = {
        std::chrono::seconds(5),
        std::chrono::seconds(15),
        std::chrono::seconds(60)
    };

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
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (running_)
    {
        return true;
    }

    logger_ = logger;
    stopRequested_ = false;
    running_ = true;

    try
    {
        workerThread_ = std::thread(&CodexQuotaMonitor::WorkerLoop, this);
    }
    catch (...)
    {
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
            PublishStatus(CodexQuotaStatus::Unavailable);
            LogWarning(L"Codex executable was not found. Quota monitoring is unavailable.");
        }
        else if (!StartAppServer(executablePath))
        {
            PublishStatus(CodexQuotaStatus::CollectorError);
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

            PublishStatus(CodexQuotaStatus::CollectorError);
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
    std::string outputBuffer;
    std::string errorBuffer;
    const auto handshakeDeadline = std::chrono::steady_clock::now() + HANDSHAKE_TIMEOUT;
    auto nextRefresh = std::chrono::steady_clock::time_point::max();
    auto nextAccountCheck = std::chrono::steady_clock::time_point::max();

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
        const bool wasAuthenticated = authenticated;
        if (!DrainOutput(outputBuffer, initialized, authenticated, receivedQuota))
        {
            return receivedQuota;
        }
        DrainErrorOutput(errorBuffer);

        if (initialized && !wasInitialized)
        {
            nextAccountCheck =
                std::chrono::steady_clock::now() + REFRESH_INTERVAL;
        }

        if (authenticated && !wasAuthenticated)
        {
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
            nextRefresh = std::chrono::steady_clock::now() + REFRESH_INTERVAL;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!initialized && now >= handshakeDeadline)
        {
            LogError(L"Codex App Server initialization timed out.");
            return receivedQuota;
        }

        if (authenticated && now >= nextRefresh)
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
            nextRefresh = now + REFRESH_INTERVAL;
        }

        if (initialized && !authenticated && now >= nextAccountCheck)
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
            nextAccountCheck = now + REFRESH_INTERVAL;
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
    bool& receivedQuota
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
                !HandleJsonLine(line, initialized, authenticated, receivedQuota))
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
    bool& receivedQuota
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
        if (logger_ != nullptr)
        {
            logger_->LogCodexAccount(true);
        }
        LogInfo(L"Codex ChatGPT authentication is available.");
        return true;

    case CodexAppServerMessageType::AuthRequired:
        authenticated = false;
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
        const CodexQuotaSnapshot previous = GetSnapshot();
        if (!message.primaryPresent)
        {
            quota.primary = previous.primary;
        }
        if (!message.secondaryPresent)
        {
            quota.secondary = previous.secondary;
        }
        if (!message.rateLimitReachedPresent)
        {
            quota.rateLimitReached = previous.rateLimitReached;
        }
        if (!quota.primary.valid && !quota.secondary.valid)
        {
            LogWarning(L"Codex quota update did not contain a usable window.");
            return true;
        }
        PublishSnapshot(quota);
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
        PublishStatus(CodexQuotaStatus::CollectorError);
        return false;
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

void CodexQuotaMonitor::PublishStatus(const CodexQuotaStatus status)
{
    CodexQuotaSnapshot snapshot;
    snapshot.status = status;
    snapshot.collectedAtUnixSeconds = CurrentUnixSeconds();
    PublishSnapshot(snapshot);
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
