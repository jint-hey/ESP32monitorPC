#ifndef SERIAL_PORT_ENUMERATOR_H
#define SERIAL_PORT_ENUMERATOR_H

#include <string>
#include <vector>

class SerialPortEnumerator
{
public:
    // Returns currently registered COM ports in numeric order.
    static std::vector<std::wstring> Enumerate();
};

#endif // SERIAL_PORT_ENUMERATOR_H
