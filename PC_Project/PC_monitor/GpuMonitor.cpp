#include "GpuMonitor.h"

#include <dxgi.h>
#include <d3dkmthk.h>

#include <cwchar>
#include <cwctype>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")

GpuMonitor::GpuMonitor()
{
    available_ = Initialize();
}

GpuMonitor::~GpuMonitor()
{
    if (query_ != nullptr)
    {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }
}

bool GpuMonitor::IsAvailable() const
{
    return available_;
}

// ============================================================
// Helper
// ============================================================

bool GpuMonitor::SameLuid(
    const LUID& a,
    const LUID& b
)
{
    return
        a.LowPart == b.LowPart &&
        a.HighPart == b.HighPart;
}

bool GpuMonitor::ContainsLuid(
    const std::vector<LUID>& list,
    const LUID& luid
)
{
    for (const auto& item : list)
    {
        if (SameLuid(item, luid))
        {
            return true;
        }
    }

    return false;
}

// ============================================================
// Initialize
// ============================================================

bool GpuMonitor::Initialize()
{
    if (!EnumerateAdapters())
    {
        return false;
    }

    PDH_STATUS status = PdhOpenQueryW(
        nullptr,
        0,
        &query_
    );

    if (status != ERROR_SUCCESS)
    {
        return false;
    }

    status = PdhAddEnglishCounterW(
        query_,
        L"\\GPU Engine(*)\\Utilization Percentage",
        0,
        &counter_
    );

    if (status != ERROR_SUCCESS)
    {
        PdhCloseQuery(query_);
        query_ = nullptr;

        return false;
    }

    // First sample.
    status = PdhCollectQueryData(query_);

    if (status != ERROR_SUCCESS)
    {
        PdhCloseQuery(query_);
        query_ = nullptr;

        return false;
    }

    return true;
}

// ============================================================
// GPU enumeration
//
// Windows 10 2004+:
// D3DKMTEnumAdapters3 is used first.
//
// Filter == 0:
//
// - ComputeOnly adapters are excluded
// - DisplayOnly adapters are excluded
//
// Then DXGI is only used to obtain the GPU name.
// ============================================================

bool GpuMonitor::EnumerateAdapters()
{
    adapters_.clear();
    usages_.clear();

    // Load dynamically so there is no need to link
    // directly against onecoreuap.lib.
    HMODULE gdi32 = LoadLibraryW(L"gdi32.dll");

    if (gdi32 == nullptr)
    {
        return EnumerateAdaptersDxgiFallback();
    }

    using EnumAdapters3Func =
        NTSTATUS(WINAPI*)(
            D3DKMT_ENUMADAPTERS3*
            );

    using CloseAdapterFunc =
        NTSTATUS(WINAPI*)(
            const D3DKMT_CLOSEADAPTER*
            );

    auto enumAdapters3 =
        reinterpret_cast<EnumAdapters3Func>(
            GetProcAddress(
                gdi32,
                "D3DKMTEnumAdapters3"
            )
            );

    auto closeAdapter =
        reinterpret_cast<CloseAdapterFunc>(
            GetProcAddress(
                gdi32,
                "D3DKMTCloseAdapter"
            )
            );

    // Older Windows fallback.
    if (enumAdapters3 == nullptr)
    {
        FreeLibrary(gdi32);

        return EnumerateAdaptersDxgiFallback();
    }

    // --------------------------------------------------------
    // First call: obtain number of adapters.
    // --------------------------------------------------------

    D3DKMT_ENUMADAPTERS3 enumInfo{};

    // Very important:
    //
    // Filter = 0
    //
    // This excludes:
    //   ComputeOnly
    //   DisplayOnly
    //
    // adapters.
    enumInfo.Filter.Value = 0;

    enumInfo.NumAdapters = 0;
    enumInfo.pAdapters = nullptr;

    NTSTATUS status =
        enumAdapters3(&enumInfo);

    // NTSTATUS < 0 means failure.
    if (status < 0 ||
        enumInfo.NumAdapters == 0)
    {
        FreeLibrary(gdi32);

        return EnumerateAdaptersDxgiFallback();
    }

    // --------------------------------------------------------
    // Second call: obtain actual adapter list.
    // --------------------------------------------------------

    std::vector<D3DKMT_ADAPTERINFO>
        kmtAdapters(enumInfo.NumAdapters);

    enumInfo.pAdapters =
        kmtAdapters.data();

    status =
        enumAdapters3(&enumInfo);

    if (status < 0)
    {
        FreeLibrary(gdi32);

        return EnumerateAdaptersDxgiFallback();
    }

    // Number actually returned.
    kmtAdapters.resize(
        enumInfo.NumAdapters
    );

    // --------------------------------------------------------
    // Collect unique LUIDs.
    // --------------------------------------------------------

    std::vector<LUID> adapterLuids;

    for (const auto& adapter :
        kmtAdapters)
    {
        if (!ContainsLuid(
            adapterLuids,
            adapter.AdapterLuid))
        {
            adapterLuids.push_back(
                adapter.AdapterLuid
            );
        }
    }

    // --------------------------------------------------------
    // Close KMT handles.
    //
    // Microsoft requires handles returned by enumeration
    // to be closed.
    // --------------------------------------------------------

    if (closeAdapter != nullptr)
    {
        for (const auto& adapter :
            kmtAdapters)
        {
            if (adapter.hAdapter != 0)
            {
                D3DKMT_CLOSEADAPTER closeInfo{};

                closeInfo.hAdapter =
                    adapter.hAdapter;

                closeAdapter(
                    &closeInfo
                );
            }
        }
    }

    FreeLibrary(gdi32);

    // --------------------------------------------------------
    // Create DXGI factory.
    //
    // DXGI is now NOT responsible for deciding how many
    // GPUs exist. It is only used to resolve:
    //
    // LUID -> GPU name
    // --------------------------------------------------------

    IDXGIFactory1* factory = nullptr;

    HRESULT hr =
        CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(
                &factory
                )
        );

    if (FAILED(hr) ||
        factory == nullptr)
    {
        return false;
    }

    // --------------------------------------------------------
    // Keep the KMT order.
    // --------------------------------------------------------

    for (const LUID& targetLuid :
        adapterLuids)
    {
        IDXGIAdapter1* foundAdapter =
            nullptr;

        DXGI_ADAPTER_DESC1 foundDesc{};

        for (UINT index = 0;; ++index)
        {
            IDXGIAdapter1* adapter =
                nullptr;

            hr = factory->EnumAdapters1(
                index,
                &adapter
            );

            if (hr == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(hr) ||
                adapter == nullptr)
            {
                continue;
            }

            DXGI_ADAPTER_DESC1 desc{};

            if (SUCCEEDED(
                adapter->GetDesc1(
                    &desc
                )))
            {
                // Ignore software adapters.
                if ((desc.Flags &
                    DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    adapter->Release();
                    continue;
                }

                // Ignore remote adapters.
                if ((desc.Flags &
                    DXGI_ADAPTER_FLAG_REMOTE) != 0)
                {
                    adapter->Release();
                    continue;
                }

                if (SameLuid(
                    desc.AdapterLuid,
                    targetLuid))
                {
                    foundDesc = desc;
                    foundAdapter = adapter;

                    break;
                }
            }

            adapter->Release();
        }

        if (foundAdapter != nullptr)
        {
            AdapterInfo info;

            info.name =
                foundDesc.Description;

            info.luid =
                foundDesc.AdapterLuid;

            // Final protection against accidental
            // duplicate LUIDs.
            bool duplicate = false;

            for (const auto& existing :
                adapters_)
            {
                if (SameLuid(
                    existing.luid,
                    info.luid))
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
            {
                adapters_.push_back(
                    info
                );

                GpuUsageInfo usage;

                usage.name =
                    info.name;

                usage.usagePercent =
                    0.0;

                usages_.push_back(
                    usage
                );
            }

            foundAdapter->Release();
        }
    }

    factory->Release();

    return !adapters_.empty();
}

// ============================================================
// DXGI fallback
//
// Used on systems where D3DKMTEnumAdapters3 does not exist.
// ============================================================

bool GpuMonitor::EnumerateAdaptersDxgiFallback()
{
    adapters_.clear();
    usages_.clear();

    IDXGIFactory1* factory = nullptr;

    HRESULT hr =
        CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(
                &factory
                )
        );

    if (FAILED(hr) ||
        factory == nullptr)
    {
        return false;
    }

    for (UINT index = 0;; ++index)
    {
        IDXGIAdapter1* adapter =
            nullptr;

        hr = factory->EnumAdapters1(
            index,
            &adapter
        );

        if (hr == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        if (FAILED(hr) ||
            adapter == nullptr)
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc{};

        if (SUCCEEDED(
            adapter->GetDesc1(
                &desc
            )))
        {
            bool ignored = false;

            if ((desc.Flags &
                DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                ignored = true;
            }

            if ((desc.Flags &
                DXGI_ADAPTER_FLAG_REMOTE) != 0)
            {
                ignored = true;
            }

            if (!ignored)
            {
                bool duplicate = false;

                for (const auto& existing :
                    adapters_)
                {
                    if (SameLuid(
                        existing.luid,
                        desc.AdapterLuid))
                    {
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate)
                {
                    AdapterInfo info;

                    info.name =
                        desc.Description;

                    info.luid =
                        desc.AdapterLuid;

                    adapters_.push_back(
                        info
                    );

                    GpuUsageInfo usage;

                    usage.name =
                        info.name;

                    usages_.push_back(
                        usage
                    );
                }
            }
        }

        adapter->Release();
    }

    factory->Release();

    return !adapters_.empty();
}

// ============================================================
// Utility
// ============================================================

std::wstring GpuMonitor::ToLower(
    std::wstring text
)
{
    for (wchar_t& c : text)
    {
        c = static_cast<wchar_t>(
            std::towlower(c)
            );
    }

    return text;
}

// ============================================================
// Parse:
//
// luid_0x00000000_0x00012345
//
// or:
//
// pid_123_luid_0x00000000_0x00012345_phys_0_eng_1...
// ============================================================

bool GpuMonitor::ParseInstanceLuid(
    const std::wstring& instanceName,
    LUID& luid
)
{
    std::wstring name =
        ToLower(instanceName);

    const std::wstring marker =
        L"luid_0x";

    size_t luidPos =
        name.find(marker);

    if (luidPos == std::wstring::npos)
    {
        return false;
    }

    size_t highStart =
        luidPos + marker.length();

    size_t lowMarker =
        name.find(
            L"_0x",
            highStart
        );

    if (lowMarker == std::wstring::npos)
    {
        return false;
    }

    std::wstring highText =
        name.substr(
            highStart,
            lowMarker - highStart
        );

    size_t lowStart =
        lowMarker + 3;

    size_t lowEnd =
        name.find(
            L'_',
            lowStart
        );

    std::wstring lowText;

    if (lowEnd ==
        std::wstring::npos)
    {
        lowText =
            name.substr(
                lowStart
            );
    }
    else
    {
        lowText =
            name.substr(
                lowStart,
                lowEnd - lowStart
            );
    }

    if (highText.empty() ||
        lowText.empty())
    {
        return false;
    }

    wchar_t* highEnd = nullptr;
    wchar_t* lowEndPtr = nullptr;

    unsigned long high =
        std::wcstoul(
            highText.c_str(),
            &highEnd,
            16
        );

    unsigned long low =
        std::wcstoul(
            lowText.c_str(),
            &lowEndPtr,
            16
        );

    if (highEnd ==
        highText.c_str() ||
        lowEndPtr ==
        lowText.c_str())
    {
        return false;
    }

    luid.HighPart =
        static_cast<LONG>(
            static_cast<DWORD>(
                high
                )
            );

    luid.LowPart =
        static_cast<DWORD>(
            low
            );

    return true;
}

// ============================================================
// Extract engine ID.
//
// Different processes can use the same engine.
// We sum those processes before choosing the busiest engine.
// ============================================================

std::wstring GpuMonitor::GetEngineKey(
    const std::wstring& instanceName
)
{
    std::wstring name =
        ToLower(instanceName);

    size_t physMarker =
        name.find(
            L"_phys_"
        );

    size_t engineMarker =
        name.find(
            L"_eng_"
        );

    if (physMarker ==
        std::wstring::npos ||
        engineMarker ==
        std::wstring::npos)
    {
        return name;
    }

    size_t physStart =
        physMarker + 6;

    size_t physEnd =
        name.find(
            L'_',
            physStart
        );

    size_t engineStart =
        engineMarker + 5;

    size_t engineEnd =
        name.find(
            L'_',
            engineStart
        );

    if (physEnd ==
        std::wstring::npos)
    {
        physEnd =
            name.length();
    }

    if (engineEnd ==
        std::wstring::npos)
    {
        engineEnd =
            name.length();
    }

    std::wstring phys =
        name.substr(
            physStart,
            physEnd - physStart
        );

    std::wstring engine =
        name.substr(
            engineStart,
            engineEnd - engineStart
        );

    return phys + L":" + engine;
}

// ============================================================
// GPU Usage
// ============================================================

const std::vector<GpuUsageInfo>&
GpuMonitor::GetUsages()
{
    for (auto& usage : usages_)
    {
        usage.usagePercent = 0.0;
    }

    if (!available_)
    {
        return usages_;
    }

    PDH_STATUS status =
        PdhCollectQueryData(
            query_
        );

    if (status != ERROR_SUCCESS)
    {
        return usages_;
    }

    DWORD bufferSize = 0;
    DWORD itemCount = 0;

    status =
        PdhGetFormattedCounterArrayW(
            counter_,
            PDH_FMT_DOUBLE,
            &bufferSize,
            &itemCount,
            nullptr
        );

    if (status != PDH_MORE_DATA)
    {
        return usages_;
    }

    std::vector<BYTE>
        buffer(bufferSize);

    auto* items =
        reinterpret_cast<
        PDH_FMT_COUNTERVALUE_ITEM_W*
        >(buffer.data());

    status =
        PdhGetFormattedCounterArrayW(
            counter_,
            PDH_FMT_DOUBLE,
            &bufferSize,
            &itemCount,
            items
        );

    if (status != ERROR_SUCCESS)
    {
        return usages_;
    }

    std::vector<
        std::unordered_map<
        std::wstring,
        double
        >
    > engineUsage(
        adapters_.size()
    );

    // --------------------------------------------------------
    // Match every PDH item to the correct physical adapter.
    // --------------------------------------------------------

    for (DWORD i = 0;
        i < itemCount;
        ++i)
    {
        const auto& value =
            items[i].FmtValue;

        if (value.CStatus !=
            PDH_CSTATUS_VALID_DATA &&
            value.CStatus !=
            PDH_CSTATUS_NEW_DATA)
        {
            continue;
        }

        if (items[i].szName ==
            nullptr)
        {
            continue;
        }

        double percent =
            value.doubleValue;

        if (percent < 0.0)
        {
            continue;
        }

        std::wstring instanceName =
            items[i].szName;

        LUID instanceLuid{};

        if (!ParseInstanceLuid(
            instanceName,
            instanceLuid))
        {
            continue;
        }

        for (size_t gpuIndex = 0;
            gpuIndex < adapters_.size();
            ++gpuIndex)
        {
            if (!SameLuid(
                instanceLuid,
                adapters_[gpuIndex].luid))
            {
                continue;
            }

            std::wstring engineKey =
                GetEngineKey(
                    instanceName
                );

            engineUsage[gpuIndex]
                [engineKey] += percent;

            break;
        }
    }

    // --------------------------------------------------------
    // Overall GPU utilization =
    // utilization of the busiest engine.
    // --------------------------------------------------------

    for (size_t gpuIndex = 0;
        gpuIndex < engineUsage.size();
        ++gpuIndex)
    {
        double maxUsage = 0.0;

        for (const auto& engine :
            engineUsage[gpuIndex])
        {
            if (engine.second >
                maxUsage)
            {
                maxUsage =
                    engine.second;
            }
        }

        if (maxUsage < 0.0)
        {
            maxUsage = 0.0;
        }

        if (maxUsage > 100.0)
        {
            maxUsage = 100.0;
        }

        usages_[gpuIndex]
            .usagePercent =
            maxUsage;
    }

    return usages_;
}
