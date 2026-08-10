#ifndef CODEX_QUOTA_SERIAL_SENDER_H
#define CODEX_QUOTA_SERIAL_SENDER_H

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>

class CodexQuotaMonitor;
class ConsoleLogger;
class SerialCommunicator;

class CodexQuotaSerialSender
{
public:
    CodexQuotaSerialSender();
    ~CodexQuotaSerialSender();

    CodexQuotaSerialSender(const CodexQuotaSerialSender&) = delete;
    CodexQuotaSerialSender& operator=(const CodexQuotaSerialSender&) = delete;

    bool Start(
        CodexQuotaMonitor& monitor,
        SerialCommunicator& serial,
        ConsoleLogger* logger
    );
    void Stop();
    void NotifySerialConnected();
    bool IsRunning() const;

private:
    void WorkerLoop();

    mutable std::mutex controlMutex_;
    std::condition_variable condition_;
    std::thread workerThread_;
    bool running_ = false;
    bool stopRequested_ = false;
    bool forceResend_ = true;
    std::uint64_t lastSentSequence_ = std::numeric_limits<std::uint64_t>::max();

    CodexQuotaMonitor* monitor_ = nullptr;
    SerialCommunicator* serial_ = nullptr;
    ConsoleLogger* logger_ = nullptr;
};

#endif // CODEX_QUOTA_SERIAL_SENDER_H
