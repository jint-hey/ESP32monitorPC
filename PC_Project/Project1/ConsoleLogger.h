#ifndef CONSOLE_LOGGER_H
#define CONSOLE_LOGGER_H

#include "HardwareMonitor.h"
#include "PacketProtocol.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/*
===============================================================================
ConsoleLogger
===============================================================================

Purpose:

    Centralized terminal debug output module.

All debug / terminal output should go through this class.

Supported logs:

    - Hardware usage
    - Serial open / close
    - Serial TX packet
    - Serial RX raw bytes
    - Serial RX decoded packet
    - Information
    - Warning
    - Error

Global enable switch:

    logger.SetEnabled(true);
        -> terminal logs enabled

    logger.SetEnabled(false);
        -> ALL terminal debug logs disabled

Disabling logging does NOT affect:

    HardwareMonitor
    Serial communication
    Packet encoding
    Packet decoding
    ESP32 communication

Thread safety:

    Hardware monitoring and serial TX/RX run on different threads.

    All console output is protected by outputMutex_, so log messages
    will not become mixed together.

===============================================================================
*/

class ConsoleLogger
{
public:
    ConsoleLogger();

    ~ConsoleLogger();

    ConsoleLogger(
        const ConsoleLogger&
    ) = delete;

    ConsoleLogger& operator=(
        const ConsoleLogger&
        ) = delete;


    // ========================================================================
    // Global switch
    // ========================================================================

    void SetEnabled(
        bool enabled
    );

    bool IsEnabled() const;


    // ========================================================================
    // Hardware periodic logging
    // ========================================================================

    bool StartHardwareLogging(
        HardwareMonitor& monitor,
        unsigned int intervalMs = 1000
    );

    void StopHardwareLogging();


    // ========================================================================
    // Generic logs
    // ========================================================================

    void Info(
        const std::wstring& message
    );

    void Warning(
        const std::wstring& message
    );

    void Error(
        const std::wstring& message
    );


    // ========================================================================
    // Hardware
    // ========================================================================

    void LogHardware(
        const HardwareUsage& usage
    );


    // ========================================================================
    // Serial
    // ========================================================================

    void LogSerialOpen(
        const std::wstring& port,
        unsigned long baudRate,
        bool success
    );

    void LogSerialClose();

    void LogTxPacket(
        const Packet& packet,
        const std::vector<std::uint8_t>& raw
    );

    void LogRxRaw(
        const std::uint8_t* data,
        std::size_t length
    );

    void LogRxPacket(
        const Packet& packet
    );


private:
    // ========================================================================
    // Global logging state
    // ========================================================================

    std::atomic<bool>
        enabled_{ true };


    // ========================================================================
    // Protect terminal output
    // ========================================================================

    mutable std::mutex
        outputMutex_;


    // ========================================================================
    // Hardware logging worker
    // ========================================================================

    HardwareMonitor*
        monitor_ = nullptr;

    std::thread
        hardwareThread_;

    std::mutex
        controlMutex_;

    std::condition_variable
        condition_;

    bool hardwareLoggingRunning_ =
        false;

    bool stopRequested_ =
        false;

    std::chrono::milliseconds
        hardwareLogInterval_{ 1000 };


private:
    void HardwareLogWorker();


    static const wchar_t*
        PacketTypeName(
            PacketType type
        );


    static std::wstring
        BytesToHex(
            const std::uint8_t* data,
            std::size_t length
        );
};

#endif // CONSOLE_LOGGER_H
