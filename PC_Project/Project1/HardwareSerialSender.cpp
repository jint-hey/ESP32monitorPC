#include "HardwareSerialSender.h"

#include "HardwarePacketEncoder.h"

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
                // Send HardwareInfo once.
                // ------------------------------------------------------------

                if (!hardwareInfoSent_)
                {
                    const auto infoPayload =
                        HardwarePacketEncoder::
                        BuildInfoPayload(
                            usage
                        );

                    hardwareInfoSent_ =
                        serial->SendPacket(
                            PacketType::
                            HardwareInfo,
                            infoPayload
                        );
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
