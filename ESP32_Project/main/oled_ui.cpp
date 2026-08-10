#include "oled_ui.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"

namespace
{
constexpr int LABEL_X = 0;
constexpr int NAME_X = 30;
constexpr int VALUE_X = 92;
constexpr int NAME_WIDTH = VALUE_X - NAME_X;

constexpr uint64_t EDGE_PAUSE_MS = 800;
constexpr uint64_t PIXEL_TIME_MS = 45;
constexpr uint64_t ROW_PHASE_MS = 250;
constexpr TickType_t UI_REFRESH_PERIOD = pdMS_TO_TICKS(100);
}

OledUi::OledUi(OledDisplay& display, HardwareStateStore& state)
    : display_(display), state_(state)
{}

esp_err_t OledUi::Start()
{
    if (task_ != nullptr)
    {
        return ESP_OK;
    }

    const BaseType_t result = xTaskCreate(
        TaskEntry,
        "oled_ui",
        4096,
        this,
        5,
        &task_);
    if (result != pdPASS)
    {
        task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void OledUi::TaskEntry(void* context)
{
    static_cast<OledUi*>(context)->TaskLoop();
}

void OledUi::TaskLoop()
{
    while (true)
    {
        const HardwareSnapshot snapshot = state_.Copy();
        display_.Clear();

        if (snapshot.hasUsage)
        {
            const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000U;
            DrawDashboard(snapshot, nowMs);
        }
        else
        {
            DrawWaitingScreen();
        }

        display_.Refresh();
        vTaskDelay(UI_REFRESH_PERIOD);
    }
}

void OledUi::DrawWaitingScreen()
{
    display_.DrawText(22, 18, "PC MONITOR");
    display_.DrawText(13, 38, "WAITING UART0");
}

void OledUi::DrawDashboard(const HardwareSnapshot& snapshot, const uint64_t nowMs)
{
    const std::size_t rowCount = 2U + std::max<std::size_t>(1U, snapshot.gpuCount);
    const int rowHeight = OledDisplay::HEIGHT / static_cast<int>(rowCount);
    const int firstY = (rowHeight - OledDisplay::CHARACTER_HEIGHT) / 2;

    DrawUsageRow(firstY, "CPU", "", snapshot.cpuUsageX100, nowMs, 0);
    DrawUsageRow(firstY + rowHeight, "MEM", "", snapshot.memoryUsageX100, nowMs, 1);

    if (snapshot.gpuCount == 0)
    {
        DrawUsageRow(firstY + 2 * rowHeight, "GPU", "N/A", 0, nowMs, 2);
        return;
    }

    for (std::size_t gpuIndex = 0; gpuIndex < snapshot.gpuCount; ++gpuIndex)
    {
        const std::string_view label = snapshot.gpuCount == 1
                                           ? std::string_view("GPU")
                                           : (gpuIndex == 0 ? std::string_view("GPU0")
                                                            : std::string_view("GPU1"));
        const std::string displayName = ToDisplayText(snapshot.gpuNames[gpuIndex].data());
        DrawUsageRow(firstY + static_cast<int>(gpuIndex + 2) * rowHeight,
                     label,
                     displayName,
                     snapshot.gpuUsageX100[gpuIndex],
                     nowMs,
                     gpuIndex + 2);
    }
}

void OledUi::DrawUsageRow(const int y,
                          const std::string_view label,
                          const std::string_view name,
                          const uint16_t usageX100,
                          const uint64_t nowMs,
                          const std::size_t rowIndex)
{
    display_.DrawText(LABEL_X, y, label);

    if (!name.empty())
    {
        const int offset = ScrollOffset(name.size(), NAME_WIDTH, nowMs, rowIndex);
        display_.DrawTextClipped(NAME_X - offset, y, name, NAME_X, NAME_WIDTH);
    }

    std::array<char, 8> percentage{};
    const unsigned int whole = usageX100 / 100U;
    const unsigned int decimal = (usageX100 % 100U) / 10U;
    std::snprintf(percentage.data(), percentage.size(), "%3u.%1u%%", whole, decimal);
    display_.DrawText(VALUE_X, y, percentage.data());
}

std::string OledUi::ToDisplayText(const char* utf8Text)
{
    std::string result;
    if (utf8Text == nullptr)
    {
        return result;
    }

    const auto* cursor = reinterpret_cast<const unsigned char*>(utf8Text);
    while (*cursor != 0)
    {
        if (*cursor >= 32 && *cursor <= 126)
        {
            result.push_back(static_cast<char>(*cursor));
            ++cursor;
            continue;
        }

        result.push_back('?');
        ++cursor;
        while ((*cursor & 0xC0U) == 0x80U)
        {
            ++cursor;
        }
    }
    return result;
}

int OledUi::ScrollOffset(const std::size_t characterCount,
                         const int viewportWidth,
                         uint64_t nowMs,
                         const std::size_t rowIndex)
{
    const int textWidth = static_cast<int>(characterCount) * OledDisplay::CHARACTER_WIDTH;
    const int travelPixels = std::max(0, textWidth - viewportWidth);
    if (travelPixels == 0)
    {
        return 0;
    }

    const uint64_t travelTime = static_cast<uint64_t>(travelPixels) * PIXEL_TIME_MS;
    const uint64_t cycleTime = 2U * EDGE_PAUSE_MS + 2U * travelTime;
    nowMs = (nowMs + rowIndex * ROW_PHASE_MS) % cycleTime;

    if (nowMs < EDGE_PAUSE_MS)
    {
        return 0;
    }
    nowMs -= EDGE_PAUSE_MS;

    if (nowMs < travelTime)
    {
        return static_cast<int>(nowMs / PIXEL_TIME_MS);
    }
    nowMs -= travelTime;

    if (nowMs < EDGE_PAUSE_MS)
    {
        return travelPixels;
    }
    nowMs -= EDGE_PAUSE_MS;

    return travelPixels - static_cast<int>(std::min(nowMs, travelTime) / PIXEL_TIME_MS);
}
