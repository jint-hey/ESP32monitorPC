#include "ConsoleLogger.h"

#include <iomanip>
#include <iostream>
#include <sstream>

// ============================================================================
// Constructor / Destructor
// ============================================================================

ConsoleLogger::ConsoleLogger()
{}

ConsoleLogger::~ConsoleLogger()
{
    StopHardwareLogging();
}


// ============================================================================
// Enable / Disable
// ============================================================================

void ConsoleLogger::SetEnabled(
    bool enabled
)
{
    enabled_.store(
        enabled
    );
}

bool ConsoleLogger::IsEnabled() const
{
    return enabled_.load();
}


// ============================================================================
// Hardware logging start
// ============================================================================

bool ConsoleLogger::StartHardwareLogging(
    HardwareMonitor& monitor,
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

        if (hardwareLoggingRunning_)
        {
            return true;
        }

        monitor_ =
            &monitor;

        hardwareLogInterval_ =
            std::chrono::milliseconds(
                intervalMs
            );

        stopRequested_ =
            false;

        hardwareLoggingRunning_ =
            true;
    }


    try
    {
        hardwareThread_ =
            std::thread(
                &ConsoleLogger::
                HardwareLogWorker,
                this
            );
    }
    catch (...)
    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        hardwareLoggingRunning_ =
            false;

        monitor_ =
            nullptr;

        return false;
    }

    return true;
}


// ============================================================================
// Hardware logging stop
// ============================================================================

void ConsoleLogger::StopHardwareLogging()
{
    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        if (!hardwareLoggingRunning_)
        {
            return;
        }

        stopRequested_ =
            true;
    }


    condition_.notify_all();


    if (hardwareThread_.joinable())
    {
        hardwareThread_.join();
    }


    {
        std::lock_guard<std::mutex>
            lock(controlMutex_);

        hardwareLoggingRunning_ =
            false;

        stopRequested_ =
            false;

        monitor_ =
            nullptr;
    }
}


// ============================================================================
// Hardware worker
// ============================================================================

void ConsoleLogger::HardwareLogWorker()
{
    using Clock =
        std::chrono::steady_clock;


    std::unique_lock<std::mutex>
        lock(controlMutex_);


    auto nextPrint =
        Clock::now()
        +
        hardwareLogInterval_;


    while (!stopRequested_)
    {
        condition_.wait_until(
            lock,
            nextPrint,
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


        lock.unlock();


        /*
            Even when logging is disabled, the thread still follows
            its low-frequency timing schedule.

            No terminal output will occur.
        */

        if (enabled_.load() &&
            monitor != nullptr)
        {
            const HardwareUsage usage =
                monitor->GetUsage();


            /*
                sequence == 0 means HardwareMonitor has not completed
                its first sample yet.
            */

            if (usage.sequence != 0)
            {
                LogHardware(
                    usage
                );
            }
        }


        lock.lock();


        nextPrint +=
            hardwareLogInterval_;


        const auto now =
            Clock::now();


        if (nextPrint <= now)
        {
            nextPrint =
                now
                +
                hardwareLogInterval_;
        }
    }
}


// ============================================================================
// Generic info
// ============================================================================

void ConsoleLogger::Info(
    const std::wstring& message
)
{
    if (!enabled_.load())
    {
        return;
    }


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << L"[INFO] "
        << message
        << L"\n";
}


// ============================================================================
// Warning
// ============================================================================

void ConsoleLogger::Warning(
    const std::wstring& message
)
{
    if (!enabled_.load())
    {
        return;
    }


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << L"[WARNING] "
        << message
        << L"\n";
}


// ============================================================================
// Error
// ============================================================================

void ConsoleLogger::Error(
    const std::wstring& message
)
{
    if (!enabled_.load())
    {
        return;
    }


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcerr
        << L"[ERROR] "
        << message
        << L"\n";
}


// ============================================================================
// Hardware
// ============================================================================

void ConsoleLogger::LogHardware(
    const HardwareUsage& usage
)
{
    if (!enabled_.load())
    {
        return;
    }


    std::wostringstream stream;


    stream
        << std::fixed
        << std::setprecision(2);


    stream
        << L"\n[HARDWARE]\n";


    stream
        << L"  Sequence: "
        << usage.sequence
        << L"\n";


    stream
        << L"  CPU     : "
        << usage.cpu
        << L"%\n";


    stream
        << L"  Memory  : "
        << usage.memory
        << L"%\n";


    stream
        << L"  GPU Count: "
        << usage.gpus.size()
        << L"\n";


    for (std::size_t i = 0;
        i < usage.gpus.size();
        ++i)
    {
        stream
            << L"  GPU "
            << i
            << L" ["
            << usage.gpus[i].name
            << L"] : "
            << usage.gpus[i].usagePercent
            << L"%\n";
    }


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << stream.str();
}


// ============================================================================
// Serial open
// ============================================================================

void ConsoleLogger::LogSerialOpen(
    const std::wstring& port,
    unsigned long baudRate,
    bool success
)
{
    if (!enabled_.load())
    {
        return;
    }


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << L"\n[SERIAL OPEN]\n"

        << L"  Port    : "
        << port
        << L"\n"

        << L"  Baud    : "
        << baudRate
        << L"\n"

        << L"  Status  : "
        << (
            success
            ? L"OK"
            : L"FAILED"
            )
        << L"\n";
}


// ============================================================================
// Serial close
// ============================================================================

void ConsoleLogger::LogSerialClose()
{
    if (!enabled_.load())
    {
        return;
    }


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << L"\n[SERIAL CLOSE]\n";
}


// ============================================================================
// Packet type name
// ============================================================================

const wchar_t*
ConsoleLogger::PacketTypeName(
    PacketType type
)
{
    switch (type)
    {
    case PacketType::HardwareInfo:

        return L"HardwareInfo";


    case PacketType::HardwareUsage:

        return L"HardwareUsage";


    case PacketType::CodexQuota:

        return L"CodexQuota";


    case PacketType::Ping:

        return L"Ping";


    case PacketType::Pong:

        return L"Pong";


    case PacketType::Command:

        return L"Command";


    case PacketType::Ack:

        return L"Ack";


    case PacketType::Error:

        return L"Error";


    default:

        return L"Unknown";
    }
}


// ============================================================================
// Binary -> HEX
// ============================================================================

std::wstring ConsoleLogger::BytesToHex(
    const std::uint8_t* data,
    std::size_t length
)
{
    std::wostringstream stream;


    stream
        << std::uppercase
        << std::hex
        << std::setfill(L'0');


    for (std::size_t i = 0;
        i < length;
        ++i)
    {
        stream
            << std::setw(2)
            << static_cast<unsigned int>(
                data[i]
                );


        if (i + 1 < length)
        {
            stream << L' ';
        }
    }


    return stream.str();
}


// ============================================================================
// TX packet
// ============================================================================

void ConsoleLogger::LogTxPacket(
    const Packet& packet,
    const std::vector<std::uint8_t>& raw
)
{
    if (!enabled_.load())
    {
        return;
    }


    std::wostringstream stream;


    stream
        << L"\n[SERIAL TX]\n";


    stream
        << L"  Type    : "
        << PacketTypeName(
            packet.type
        );


    stream
        << L" (0x"
        << std::uppercase
        << std::hex
        << std::setw(2)
        << std::setfill(L'0')
        << static_cast<unsigned int>(
            packet.type
            )
        << L")\n";


    stream
        << std::dec
        << std::setfill(L' ');


    stream
        << L"  Sequence: "
        << packet.sequence
        << L"\n";


    stream
        << L"  Payload : "
        << packet.payload.size()
        << L" bytes\n";


    stream
        << L"  Total   : "
        << raw.size()
        << L" bytes\n";


    stream
        << L"  HEX     : "
        << BytesToHex(
            raw.data(),
            raw.size()
        )
        << L"\n";


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << stream.str();
}


// ============================================================================
// RX raw
// ============================================================================

void ConsoleLogger::LogRxRaw(
    const std::uint8_t* data,
    std::size_t length
)
{
    if (!enabled_.load() ||
        data == nullptr ||
        length == 0)
    {
        return;
    }


    std::wostringstream stream;


    stream
        << L"\n[SERIAL RX RAW]\n"

        << L"  Bytes   : "
        << length
        << L"\n"

        << L"  HEX     : "
        << BytesToHex(
            data,
            length
        )
        << L"\n";


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << stream.str();
}


// ============================================================================
// RX packet
// ============================================================================

void ConsoleLogger::LogRxPacket(
    const Packet& packet
)
{
    if (!enabled_.load())
    {
        return;
    }


    std::wostringstream stream;


    stream
        << L"\n[SERIAL RX PACKET]\n"

        << L"  Type    : "
        << PacketTypeName(
            packet.type
        )

        << L"\n"

        << L"  Sequence: "
        << packet.sequence
        << L"\n"

        << L"  Payload : "
        << packet.payload.size()
        << L" bytes\n"

        << L"  CRC     : OK\n";


    if (!packet.payload.empty())
    {
        stream
            << L"  Payload HEX: "
            << BytesToHex(
                packet.payload.data(),
                packet.payload.size()
            )
            << L"\n";
    }


    std::lock_guard<std::mutex>
        lock(outputMutex_);


    std::wcout
        << stream.str();
}
