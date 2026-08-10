#include "SerialPort.h"

#include <algorithm>

SerialPort::SerialPort()
{}

SerialPort::~SerialPort()
{
    Close();
}

// ============================================================================
// Open
// ============================================================================

bool SerialPort::Open(
    const std::wstring& portName,
    DWORD baudRate
)
{
    Close();

    std::wstring fullName =
        portName;

    /*
        COM1 ~ COM9 can sometimes work without this prefix,
        but COM10+ requires:

            \\.\COM10

        Always using the prefix makes the code generic.
    */
    if (fullName.rfind(
        L"\\\\.\\",
        0
    ) != 0)
    {
        fullName =
            L"\\\\.\\" + fullName;
    }

    handle_ =
        CreateFileW(
            fullName.c_str(),
            GENERIC_READ |
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_OVERLAPPED,
            nullptr
        );

    if (handle_ ==
        INVALID_HANDLE_VALUE)
    {
        return false;
    }

    SetupComm(
        handle_,
        4096,
        4096
    );

    DCB dcb{};
    dcb.DCBlength =
        sizeof(DCB);

    if (!GetCommState(
        handle_,
        &dcb))
    {
        Close();
        return false;
    }

    dcb.BaudRate =
        baudRate;

    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;

    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;

    dcb.fDtrControl =
        DTR_CONTROL_DISABLE;

    dcb.fRtsControl =
        RTS_CONTROL_DISABLE;

    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;

    if (!SetCommState(
        handle_,
        &dcb))
    {
        Close();
        return false;
    }

    COMMTIMEOUTS timeouts{};

    if (!SetCommTimeouts(
        handle_,
        &timeouts))
    {
        Close();
        return false;
    }

    /*
        WaitCommEvent will wake when serial RX data arrives.
    */
    if (!SetCommMask(
        handle_,
        EV_RXCHAR |
        EV_ERR))
    {
        Close();
        return false;
    }

    PurgeComm(
        handle_,
        PURGE_RXCLEAR |
        PURGE_TXCLEAR |
        PURGE_RXABORT |
        PURGE_TXABORT
    );

    return true;
}

// ============================================================================
// Close
// ============================================================================

void SerialPort::Close()
{
    if (handle_ ==
        INVALID_HANDLE_VALUE)
    {
        return;
    }

    CancelIoEx(
        handle_,
        nullptr
    );

    CloseHandle(
        handle_
    );

    handle_ =
        INVALID_HANDLE_VALUE;
}

bool SerialPort::IsOpen() const
{
    return
        handle_ !=
        INVALID_HANDLE_VALUE;
}

void SerialPort::CancelPendingIO()
{
    if (!IsOpen())
    {
        return;
    }

    CancelIoEx(
        handle_,
        nullptr
    );
}

// ============================================================================
// Read
// ============================================================================

bool SerialPort::ReadSome(
    std::uint8_t* buffer,
    DWORD bufferSize,
    DWORD& bytesRead,
    DWORD timeoutMs
)
{
    bytesRead = 0;

    if (!IsOpen() ||
        buffer == nullptr ||
        bufferSize == 0)
    {
        return false;
    }

    // ------------------------------------------------------------------------
    // First wait until data arrives.
    // ------------------------------------------------------------------------

    OVERLAPPED waitOverlapped{};

    waitOverlapped.hEvent =
        CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

    if (waitOverlapped.hEvent ==
        nullptr)
    {
        return false;
    }

    DWORD eventMask = 0;

    BOOL waitStarted =
        WaitCommEvent(
            handle_,
            &eventMask,
            &waitOverlapped
        );

    if (!waitStarted)
    {
        const DWORD error =
            GetLastError();

        if (error !=
            ERROR_IO_PENDING)
        {
            CloseHandle(
                waitOverlapped.hEvent
            );

            return false;
        }

        const DWORD result =
            WaitForSingleObject(
                waitOverlapped.hEvent,
                timeoutMs
            );

        if (result ==
            WAIT_TIMEOUT)
        {
            CancelIoEx(
                handle_,
                &waitOverlapped
            );

            // OVERLAPPED must not leave scope while I/O is pending.
            WaitForSingleObject(
                waitOverlapped.hEvent,
                INFINITE
            );

            CloseHandle(
                waitOverlapped.hEvent
            );

            // Timeout is not considered a serial error.
            return true;
        }

        if (result !=
            WAIT_OBJECT_0)
        {
            CancelIoEx(
                handle_,
                &waitOverlapped
            );

            WaitForSingleObject(
                waitOverlapped.hEvent,
                INFINITE
            );

            CloseHandle(
                waitOverlapped.hEvent
            );

            return false;
        }

        DWORD transferred = 0;

        if (!GetOverlappedResult(
            handle_,
            &waitOverlapped,
            &transferred,
            FALSE))
        {
            const DWORD resultError =
                GetLastError();

            CloseHandle(
                waitOverlapped.hEvent
            );

            if (resultError ==
                ERROR_OPERATION_ABORTED)
            {
                return false;
            }

            return false;
        }
    }

    CloseHandle(
        waitOverlapped.hEvent
    );

    if ((eventMask &
        EV_RXCHAR) == 0)
    {
        return true;
    }

    // ------------------------------------------------------------------------
    // Find out how many bytes are already in the RX queue.
    // ------------------------------------------------------------------------

    DWORD errors = 0;
    COMSTAT stat{};

    if (!ClearCommError(
        handle_,
        &errors,
        &stat))
    {
        return false;
    }

    if (stat.cbInQue == 0)
    {
        return true;
    }

    const DWORD toRead =
        (std::min)(
            bufferSize,
            stat.cbInQue
            );

    // ------------------------------------------------------------------------
    // Read available bytes.
    // ------------------------------------------------------------------------

    OVERLAPPED readOverlapped{};

    readOverlapped.hEvent =
        CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

    if (readOverlapped.hEvent ==
        nullptr)
    {
        return false;
    }

    BOOL readStarted =
        ReadFile(
            handle_,
            buffer,
            toRead,
            &bytesRead,
            &readOverlapped
        );

    if (!readStarted)
    {
        const DWORD error =
            GetLastError();

        if (error !=
            ERROR_IO_PENDING)
        {
            CloseHandle(
                readOverlapped.hEvent
            );

            return false;
        }

        const DWORD result =
            WaitForSingleObject(
                readOverlapped.hEvent,
                timeoutMs
            );

        if (result !=
            WAIT_OBJECT_0)
        {
            CancelIoEx(
                handle_,
                &readOverlapped
            );

            WaitForSingleObject(
                readOverlapped.hEvent,
                INFINITE
            );

            CloseHandle(
                readOverlapped.hEvent
            );

            return false;
        }

        if (!GetOverlappedResult(
            handle_,
            &readOverlapped,
            &bytesRead,
            FALSE))
        {
            CloseHandle(
                readOverlapped.hEvent
            );

            return false;
        }
    }

    CloseHandle(
        readOverlapped.hEvent
    );

    return true;
}

// ============================================================================
// Write
// ============================================================================

bool SerialPort::Write(
    const std::uint8_t* data,
    DWORD length,
    DWORD timeoutMs
)
{
    if (!IsOpen() ||
        data == nullptr)
    {
        return false;
    }

    DWORD totalWritten = 0;

    while (totalWritten < length)
    {
        OVERLAPPED overlapped{};

        overlapped.hEvent =
            CreateEventW(
                nullptr,
                TRUE,
                FALSE,
                nullptr
            );

        if (overlapped.hEvent ==
            nullptr)
        {
            return false;
        }

        DWORD written = 0;

        BOOL started =
            WriteFile(
                handle_,
                data + totalWritten,
                length - totalWritten,
                &written,
                &overlapped
            );

        if (!started)
        {
            const DWORD error =
                GetLastError();

            if (error !=
                ERROR_IO_PENDING)
            {
                CloseHandle(
                    overlapped.hEvent
                );

                return false;
            }

            const DWORD result =
                WaitForSingleObject(
                    overlapped.hEvent,
                    timeoutMs
                );

            if (result !=
                WAIT_OBJECT_0)
            {
                CancelIoEx(
                    handle_,
                    &overlapped
                );

                WaitForSingleObject(
                    overlapped.hEvent,
                    INFINITE
                );

                CloseHandle(
                    overlapped.hEvent
                );

                return false;
            }

            if (!GetOverlappedResult(
                handle_,
                &overlapped,
                &written,
                FALSE))
            {
                CloseHandle(
                    overlapped.hEvent
                );

                return false;
            }
        }

        CloseHandle(
            overlapped.hEvent
        );

        if (written == 0)
        {
            return false;
        }

        totalWritten += written;
    }

    return true;
}
