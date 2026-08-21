#ifndef CODEX_QUOTA_MONITOR_H
#define CODEX_QUOTA_MONITOR_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include "CodexQuota.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class ConsoleLogger;

class CodexQuotaMonitor
{
public:
    using StatusCallback = std::function<void(CodexQuotaStatus)>;

    CodexQuotaMonitor();
    ~CodexQuotaMonitor();

    CodexQuotaMonitor(const CodexQuotaMonitor&) = delete;
    CodexQuotaMonitor& operator=(const CodexQuotaMonitor&) = delete;

    void SetStatusCallback(StatusCallback callback);
    bool Start(ConsoleLogger* logger);
    void Stop();
    bool IsRunning() const;
    CodexQuotaSnapshot GetSnapshot() const;

private:
    static constexpr std::size_t MAX_JSON_LINE_SIZE = 1024U * 1024U;

    void WorkerLoop();
    bool StartAppServer(const std::wstring& executablePath);
    bool RunAppServerSession();
    void CloseAppServer(bool terminateProcess);

    bool WriteJsonLine(const char* json);
    bool DrainOutput(
        std::string& lineBuffer,
        bool& initialized,
        bool& authenticated,
        bool& receivedQuota,
        bool& accountResponseReceived,
        bool& quotaResponseReceived,
        bool& accountRequestFailed,
        bool& quotaRequestFailed
    );
    void DrainErrorOutput(std::string& errorBuffer);
    bool HandleJsonLine(
        const std::string& line,
        bool& initialized,
        bool& authenticated,
        bool& receivedQuota,
        bool& accountResponseReceived,
        bool& quotaResponseReceived,
        bool& accountRequestFailed,
        bool& quotaRequestFailed
    );

    void PublishSnapshot(CodexQuotaSnapshot snapshot);
    void PublishStatus(CodexQuotaStatus status, bool preserveQuota = false);
    void ReportCollectorFailure();
    void LoadCachedSnapshot();
    void SaveCachedSnapshot(const CodexQuotaSnapshot& snapshot) const;
    void LogInfo(const std::wstring& message) const;
    void LogWarning(const std::wstring& message) const;
    void LogError(const std::wstring& message) const;

    static std::wstring FindCodexExecutable();
    static std::wstring Utf8ToWide(std::string_view text);
    static std::uint64_t CurrentUnixSeconds();

    mutable std::mutex snapshotMutex_;
    CodexQuotaSnapshot snapshot_;
    std::uint64_t nextSequence_ = 0;
    bool hasPublishedStatus_ = false;
    std::uint32_t consecutiveCollectorFailures_ = 0;

    mutable std::mutex callbackMutex_;
    StatusCallback statusCallback_;

    mutable std::mutex controlMutex_;
    std::condition_variable condition_;
    std::thread workerThread_;
    std::atomic<bool> running_{ false };
    bool stopRequested_ = false;

    ConsoleLogger* logger_ = nullptr;

    HANDLE processHandle_ = nullptr;
    HANDLE jobHandle_ = nullptr;
    HANDLE standardInputWrite_ = nullptr;
    HANDLE standardOutputRead_ = nullptr;
    HANDLE standardErrorRead_ = nullptr;
};

#endif // CODEX_QUOTA_MONITOR_H
