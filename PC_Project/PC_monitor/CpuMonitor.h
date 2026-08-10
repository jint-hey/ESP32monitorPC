#pragma once

#include <Windows.h>

class CpuMonitor
{
public:
    CpuMonitor();

    // 返回 CPU 使用率：0.0 ~ 100.0
    double GetUsage();

private:
    ULARGE_INTEGER lastIdleTime_{};
    ULARGE_INTEGER lastKernelTime_{};
    ULARGE_INTEGER lastUserTime_{};

    bool initialized_ = false;

    static ULARGE_INTEGER FileTimeToUInt64(const FILETIME& ft);
};
