#include "CpuMonitor.h"

CpuMonitor::CpuMonitor()
{
    // 第一次调用用于建立采样基准
    GetUsage();
}

ULARGE_INTEGER CpuMonitor::FileTimeToUInt64(const FILETIME& ft)
{
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;

    return value;
}

double CpuMonitor::GetUsage()
{
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};

    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
        return -1.0;
    }

    ULARGE_INTEGER idle = FileTimeToUInt64(idleTime);
    ULARGE_INTEGER kernel = FileTimeToUInt64(kernelTime);
    ULARGE_INTEGER user = FileTimeToUInt64(userTime);

    if (!initialized_)
    {
        lastIdleTime_ = idle;
        lastKernelTime_ = kernel;
        lastUserTime_ = user;

        initialized_ = true;

        return 0.0;
    }

    ULONGLONG idleDiff =
        idle.QuadPart - lastIdleTime_.QuadPart;

    ULONGLONG kernelDiff =
        kernel.QuadPart - lastKernelTime_.QuadPart;

    ULONGLONG userDiff =
        user.QuadPart - lastUserTime_.QuadPart;

    ULONGLONG totalDiff =
        kernelDiff + userDiff;

    double usage = 0.0;

    if (totalDiff > 0)
    {
        usage =
            (1.0 -
                static_cast<double>(idleDiff) /
                static_cast<double>(totalDiff))
            * 100.0;
    }

    lastIdleTime_ = idle;
    lastKernelTime_ = kernel;
    lastUserTime_ = user;

    // 防止极端情况下浮点误差
    if (usage < 0.0)
        usage = 0.0;

    if (usage > 100.0)
        usage = 100.0;

    return usage;
}
