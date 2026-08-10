#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

class SerialPort
{
public:
    SerialPort();

    ~SerialPort();

    SerialPort(
        const SerialPort&
    ) = delete;

    SerialPort& operator=(
        const SerialPort&
        ) = delete;

    bool Open(
        const std::wstring& portName,
        DWORD baudRate = 115200
    );

    void Close();

    bool IsOpen() const;

    /*
        Read available serial data.

        timeoutMs is only used inside the dedicated RX thread.
        It does NOT block the main application thread.
    */
    bool ReadSome(
        std::uint8_t* buffer,
        DWORD bufferSize,
        DWORD& bytesRead,
        DWORD timeoutMs = 200
    );

    bool Write(
        const std::uint8_t* data,
        DWORD length,
        DWORD timeoutMs = 2000
    );

    /*
        Cancel pending overlapped I/O.

        Used when SerialCommunicator is being stopped.
    */
    void CancelPendingIO();

private:
    HANDLE handle_ =
        INVALID_HANDLE_VALUE;
};

#endif
