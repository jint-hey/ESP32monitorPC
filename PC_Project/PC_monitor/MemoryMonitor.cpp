#include "MemoryMonitor.h"

#include <Windows.h>

double MemoryMonitor::GetUsage() const
{
    MEMORYSTATUSEX memoryStatus{};

    memoryStatus.dwLength =
        sizeof(MEMORYSTATUSEX);

    if (!GlobalMemoryStatusEx(&memoryStatus))
    {
        return -1.0;
    }

    return static_cast<double>(
        memoryStatus.dwMemoryLoad
        );
}
