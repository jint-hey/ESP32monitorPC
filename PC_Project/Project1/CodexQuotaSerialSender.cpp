#include "CodexQuotaSerialSender.h"

#include "CodexQuotaMonitor.h"
#include "CodexQuotaPacketEncoder.h"
#include "ConsoleLogger.h"
#include "PacketProtocol.h"
#include "SerialCommunicator.h"

#include <chrono>
#include <sstream>

CodexQuotaSerialSender::CodexQuotaSerialSender() = default;

CodexQuotaSerialSender::~CodexQuotaSerialSender()
{
    Stop();
}

bool CodexQuotaSerialSender::Start(
    CodexQuotaMonitor& monitor,
    SerialCommunicator& serial,
    ConsoleLogger* logger
)
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    if (running_)
    {
        return true;
    }

    monitor_ = &monitor;
    serial_ = &serial;
    logger_ = logger;
    stopRequested_ = false;
    forceResend_ = true;
    lastSentSequence_ = std::numeric_limits<std::uint64_t>::max();
    running_ = true;

    try
    {
        workerThread_ = std::thread(&CodexQuotaSerialSender::WorkerLoop, this);
    }
    catch (...)
    {
        running_ = false;
        monitor_ = nullptr;
        serial_ = nullptr;
        logger_ = nullptr;
        return false;
    }
    return true;
}

void CodexQuotaSerialSender::Stop()
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
    monitor_ = nullptr;
    serial_ = nullptr;
    logger_ = nullptr;
}

void CodexQuotaSerialSender::NotifySerialConnected()
{
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        forceResend_ = true;
    }
    condition_.notify_all();
}

bool CodexQuotaSerialSender::IsRunning() const
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return running_;
}

void CodexQuotaSerialSender::WorkerLoop()
{
    using namespace std::chrono_literals;

    std::unique_lock<std::mutex> lock(controlMutex_);
    while (!stopRequested_)
    {
        CodexQuotaMonitor* monitor = monitor_;
        SerialCommunicator* serial = serial_;
        ConsoleLogger* logger = logger_;
        const bool forceResend = forceResend_;
        forceResend_ = false;
        const std::uint64_t lastSent = lastSentSequence_;

        lock.unlock();

        if (monitor != nullptr && serial != nullptr && serial->IsRunning())
        {
            const CodexQuotaSnapshot snapshot = monitor->GetSnapshot();
            if (forceResend || snapshot.sequence != lastSent)
            {
                const std::vector<std::uint8_t> payload =
                    CodexQuotaPacketEncoder::BuildPayload(snapshot);

                if (serial->SendPacket(PacketType::CodexQuota, payload))
                {
                    lock.lock();
                    lastSentSequence_ = snapshot.sequence;
                    lock.unlock();

                    if (logger != nullptr)
                    {
                        std::wostringstream message;
                        message << L"Codex quota packet queued. status="
                                << static_cast<unsigned int>(snapshot.status);
                        if (snapshot.primary.valid)
                        {
                            message << L", primary remaining="
                                    << snapshot.primary.remainingPercentX100 / 100.0
                                    << L"%";
                        }
                        if (snapshot.secondary.valid)
                        {
                            message << L", secondary remaining="
                                    << snapshot.secondary.remainingPercentX100 / 100.0
                                    << L"%";
                        }
                        logger->Info(message.str());
                    }
                }
                else
                {
                    lock.lock();
                    forceResend_ = true;
                    lock.unlock();
                }
            }
        }

        lock.lock();
        condition_.wait_for(
            lock,
            250ms,
            [this]() { return stopRequested_ || forceResend_; }
        );
    }
}
