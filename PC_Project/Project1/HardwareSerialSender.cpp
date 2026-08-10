#include "HardwareSerialSender.h"

#include "HardwarePacketEncoder.h"

namespace
{
    // HardwareInfo contains GPU names. Resend it periodically so an ESP32
    // reset can recover the names even when Windows keeps the COM port open.
    constexpr auto HARDWARE_INFO_RESEND_INTERVAL =
        std::chrono::seconds(10);
}

HardwareSerialSender::
HardwareSerialSender()
{}

HardwareSerialSender::
~HardwareSerialSender()
{
    Stop();
}

bool HardwareSerialSender::Start(
    HardwareMonitor& monitor,
    SerialCommunicator& serial,
    unsigned int intervalMs
)
{
    if (intervalMs == 0)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        if (running_)
        {
            return true;
        }

        monitor_ =
            &monitor;

        serial_ =
            &serial;

        interval_ =
            std::chrono::milliseconds(
                intervalMs
            );

        stopRequested_ = false;

        hardwareInfoSent_ = false;

        lastHardwareSequence_ = 0;

        running_ = true;
    }

    try
    {
        workerThread_ =
            std::thread(
                &HardwareSerialSender::
                WorkerLoop,
                this
            );
    }
    catch (...)
    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        running_ = false;

        monitor_ = nullptr;
        serial_ = nullptr;

        return false;
    }

    return true;
}

void HardwareSerialSender::Stop()
{
    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

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

    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        running_ = false;
        stopRequested_ = false;

        monitor_ = nullptr;
        serial_ = nullptr;
    }
}

bool HardwareSerialSender::IsRunning() const
{
    std::lock_guard<std::mutex>
        lock(controlMutex_);

    return running_;
}

void HardwareSerialSender::WorkerLoop()
{
    using Clock =
        std::chrono::steady_clock;

    std::unique_lock<std::mutex>
        lock(controlMutex_);

    auto nextSend =
        Clock::now() + interval_;

    auto nextHardwareInfoSend =
        Clock::now();

    while (!stopRequested_)
    {
        condition_.wait_until(
            lock,
            nextSend,
            [this]()
            {
                return stopRequested_;
            }
        );

        if (stopRequested_)
        {
            break;
        }

        HardwareMonitor* monitor =
            monitor_;

        SerialCommunicator* serial =
            serial_;

        lock.unlock();

        if (monitor != nullptr &&
            serial != nullptr &&
            serial->IsRunning())
        {
            const HardwareUsage usage =
                monitor->GetUsage();

            /*
                sequence == 0 means that HardwareMonitor
                has not completed its first sample yet.
            */
            if (usage.sequence != 0 &&
                usage.sequence !=
                lastHardwareSequence_)
            {
                // ------------------------------------------------------------
                // Send HardwareInfo at startup and periodically. The latter
                // restores GPU names after an ESP32-only reset.
                // ------------------------------------------------------------

                const auto now = Clock::now();
                if (!hardwareInfoSent_ || now >= nextHardwareInfoSend)
                {
                    const auto infoPayload =
                        HardwarePacketEncoder::
                        BuildInfoPayload(
                            usage
                        );

                    const bool infoSent =
                        serial->SendPacket(
                            PacketType::
                            HardwareInfo,
                            infoPayload
                        );

                    hardwareInfoSent_ = infoSent;
                    if (infoSent)
                    {
                        nextHardwareInfoSend =
                            now + HARDWARE_INFO_RESEND_INTERVAL;
                    }
                }

                // ------------------------------------------------------------
                // Only send usage after HardwareInfo has been queued.
                // ------------------------------------------------------------

                if (hardwareInfoSent_)
                {
                    const auto usagePayload =
                        HardwarePacketEncoder::
                        BuildUsagePayload(
                            usage
                        );

                    if (serial->SendPacket(
                        PacketType::
                        HardwareUsage,
                        usagePayload))
                    {
                        lastHardwareSequence_ =
                            usage.sequence;
                    }
                }
            }
        }

        lock.lock();

        nextSend += interval_;

        const auto now =
            Clock::now();

        if (nextSend <= now)
        {
            nextSend =
                now + interval_;
        }
    }
}
