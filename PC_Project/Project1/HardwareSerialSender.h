#ifndef HARDWARE_SERIAL_SENDER_H
#define HARDWARE_SERIAL_SENDER_H

#include "HardwareMonitor.h"
#include "SerialCommunicator.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class HardwareSerialSender
{
public:
    HardwareSerialSender();

    ~HardwareSerialSender();

    HardwareSerialSender(
        const HardwareSerialSender&
    ) = delete;

    HardwareSerialSender& operator=(
        const HardwareSerialSender&
        ) = delete;

    bool Start(
        HardwareMonitor& monitor,
        SerialCommunicator& serial,
        unsigned int intervalMs = 1000
    );

    void Stop();

    bool IsRunning() const;

private:
    HardwareMonitor* monitor_ =
        nullptr;

    SerialCommunicator* serial_ =
        nullptr;

    std::thread workerThread_;

    mutable std::mutex
        controlMutex_;

    std::condition_variable
        condition_;

    bool running_ = false;

    bool stopRequested_ = false;

    bool hardwareInfoSent_ = false;

    std::uint64_t
        lastHardwareSequence_ = 0;

    std::chrono::milliseconds
        interval_{ 1000 };

private:
    void WorkerLoop();
};

#endif
