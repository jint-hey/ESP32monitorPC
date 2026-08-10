#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <string>
#include <vector>

struct GpuUsageInfo
{
    std::wstring name;
    double usagePercent = 0.0;
};

class GpuMonitor
{
public:
    GpuMonitor();
    ~GpuMonitor();

    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;

    bool IsAvailable() const;

    const std::vector<GpuUsageInfo>& GetUsages();

private:
    struct AdapterInfo
    {
        std::wstring name;
        LUID luid{};
    };

    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER counter_ = nullptr;

    bool available_ = false;

    std::vector<AdapterInfo> adapters_;
    std::vector<GpuUsageInfo> usages_;

private:
    bool Initialize();

    // Preferred enumeration method.
    bool EnumerateAdapters();

    // Used if D3DKMTEnumAdapters3 is unavailable.
    bool EnumerateAdaptersDxgiFallback();

    static bool ParseInstanceLuid(
        const std::wstring& instanceName,
        LUID& luid
    );

    static bool SameLuid(
        const LUID& a,
        const LUID& b
    );

    static bool ContainsLuid(
        const std::vector<LUID>& list,
        const LUID& luid
    );

    static std::wstring GetEngineKey(
        const std::wstring& instanceName
    );

    static std::wstring ToLower(
        std::wstring text
    );
};
