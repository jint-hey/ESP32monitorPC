#include "SerialPortEnumerator.h"

#include <Windows.h>

std::vector<std::wstring>
SerialPortEnumerator::Enumerate()
{
    std::vector<std::wstring> ports;

    // QueryDosDevice sees physical, USB and virtual COM ports without
    // requiring SetupAPI or administrator privileges.
    for (unsigned int number = 1;
        number <= 256;
        ++number)
    {
        const std::wstring portName =
            L"COM" + std::to_wstring(number);

        wchar_t target[1024]{};

        if (QueryDosDeviceW(
            portName.c_str(),
            target,
            static_cast<DWORD>(
                sizeof(target) /
                sizeof(target[0])
                )) != 0)
        {
            ports.push_back(portName);
        }
    }

    return ports;
}
