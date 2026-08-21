#include "oled_ui.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "app_config.hpp"
#include "core.hpp"
#include "esp_timer.h"
#include "unix_time.hpp"

namespace
{
constexpr int LABEL_X = 0;
constexpr int NAME_X = 30;
constexpr int VALUE_X = 92;
constexpr int NAME_WIDTH = VALUE_X - NAME_X;

constexpr uint64_t EDGE_PAUSE_MS = 800;
constexpr uint64_t PIXEL_TIME_MS = 45;
constexpr uint64_t ROW_PHASE_MS = 250;
static_assert(app_config::OLED_PAGE_SWITCH_SECONDS > 0,
              "OLED page switch interval must be greater than zero");
constexpr uint64_t PAGE_SWITCH_PERIOD_MS =
    static_cast<uint64_t>(app_config::OLED_PAGE_SWITCH_SECONDS) * 1000U;
constexpr TickType_t UI_REFRESH_PERIOD = pdMS_TO_TICKS(100);

void FormatWindowDuration(const uint32_t minutes, char* output, const std::size_t size)
{
    if (minutes != 0 && minutes % 1440U == 0)
    {
        std::snprintf(output, size, "%luD", static_cast<unsigned long>(minutes / 1440U));
    }
    else if (minutes != 0 && minutes % 60U == 0)
    {
        std::snprintf(output, size, "%luH", static_cast<unsigned long>(minutes / 60U));
    }
    else
    {
        std::snprintf(output, size, "%luM", static_cast<unsigned long>(minutes));
    }
}
}

OledUi::OledUi(OledDisplay& display,
               HardwareStateStore& hardwareState,
               CodexQuotaStateStore& codexQuotaState,
               PcConnectionStateStore& connectionState,
               router_monitor::RouterMonitorStatusStore& routerMonitorState)
    : display_(display),
      hardwareState_(hardwareState),
      codexQuotaState_(codexQuotaState),
      connectionState_(connectionState),
      routerMonitorState_(routerMonitorState)
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
        const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000U;
        display_.Clear();

        const HardwareSnapshot hardwareSnapshot = hardwareState_.Copy();
        const uint64_t page = (nowMs / PAGE_SWITCH_PERIOD_MS) % 3U;

        if (page == 2U)
        {
            DrawRouterMonitorPage(routerMonitorState_.Copy(), nowMs);
        }
        else if (!connectionState_.IsConnected() || !hardwareSnapshot.hasUsage)
        {
            DrawWaitingScreen();
        }
        else if (page == 0U)
        {
            DrawDashboard(hardwareSnapshot, nowMs);
        }
        else
        {
            DrawCodexPage(codexQuotaState_.Copy());
        }

        display_.Refresh();
        vTaskDelay(UI_REFRESH_PERIOD);
    }
}

void OledUi::DrawRouterMonitorPage(
    const router_monitor::RouterMonitorSnapshot& snapshot,
    const uint64_t nowMs)
{
    display_.DrawText(0, 0, "ROUTER DEVICE");
    if (!snapshot.has_result)
    {
        display_.DrawText(16, 24, "CHECK PENDING");
        return;
    }

    display_.DrawText(0, 11, "ST");
    display_.DrawText(30, 11, router_monitor::status_name(snapshot.status));
    DrawRouterTextRow(22, "TGT", snapshot.target.data(), nowMs, 0);
    DrawRouterTextRow(33, "NAME", snapshot.device_name.data(), nowMs, 1);
    DrawRouterTextRow(44, "IP", snapshot.ip.data(), nowMs, 2);
    DrawRouterTextRow(55, "MAC", snapshot.mac.data(), nowMs, 3);
}

void OledUi::DrawRouterTextRow(const int y,
                               const std::string_view label,
                               const char* value,
                               const uint64_t nowMs,
                               const std::size_t rowIndex)
{
    constexpr int valueX = 30;
    constexpr int valueWidth = OledDisplay::WIDTH - valueX;
    display_.DrawText(0, y, label);
    std::string displayValue = ToDisplayText(value);
    if (displayValue.empty()) displayValue = "-";
    const int offset = ScrollOffset(displayValue.size(), valueWidth, nowMs, rowIndex);
    display_.DrawTextClipped(valueX - offset, y, displayValue, valueX, valueWidth);
}

void OledUi::DrawCodexPage(const CodexQuotaSnapshot& snapshot)
{
    display_.DrawText(28, 0, "CODEX WEEKLY");

    if (!snapshot.hasPacket)
    {
        display_.DrawText(22, 24, "WAITING CODEX");
        display_.DrawText(31, 40, "UART DATA");
        return;
    }

    if (snapshot.status != CodexQuotaStatus::Valid)
    {
        switch (snapshot.status)
        {
        case CodexQuotaStatus::AuthRequired:
            display_.DrawText(22, 24, "LOGIN REQUIRED");
            break;
        case CodexQuotaStatus::CollectorError:
            display_.DrawText(13, 24, "COLLECTOR ERROR");
            break;
        case CodexQuotaStatus::Unavailable:
        default:
            display_.DrawText(25, 24, "UNAVAILABLE");
            break;
        }
        return;
    }

    if (!snapshot.quota.valid)
    {
        display_.DrawText(22, 24, "QUOTA MISSING");
        return;
    }

    std::array<char, 22> text{};
    std::snprintf(text.data(), text.size(), "LEFT %u.%u%%",
                  snapshot.quota.remainingPercentX100 / 100U,
                  (snapshot.quota.remainingPercentX100 % 100U) / 10U);
    display_.DrawText(1, 11, text.data());
    DrawProgressBar(1, 21, 126, 9, snapshot.quota.remainingPercentX100);

    std::snprintf(text.data(), text.size(), "USED %u.%u%%",
                  snapshot.quota.usedPercentX100 / 100U,
                  (snapshot.quota.usedPercentX100 % 100U) / 10U);
    display_.DrawText(1, 33, text.data());

    std::array<char, 10> duration{};
    FormatWindowDuration(snapshot.quota.windowDurationMinutes,
                         duration.data(), duration.size());
    if (snapshot.rateLimitReached)
    {
        std::snprintf(text.data(), text.size(), "LIMITED %s", duration.data());
    }
    else
    {
        std::snprintf(text.data(), text.size(), "WINDOW %s", duration.data());
    }
    display_.DrawText(1, 43, text.data());

    if (snapshot.quota.resetsAtUnixSeconds == 0)
    {
        display_.DrawText(10, 54, "RST N/A");
        return;
    }

    const LocalDateTime reset = UnixToLocalDateTime(
        snapshot.quota.resetsAtUnixSeconds,
        app_config::CODEX_TIMEZONE_OFFSET_MINUTES);
    std::snprintf(text.data(), text.size(), "RST %02u/%02u/%02u %02u:%02u",
                  static_cast<unsigned int>(reset.year % 100),
                  reset.month,
                  reset.day,
                  reset.hour,
                  reset.minute);
    display_.DrawText(10, 54, text.data());
}

void OledUi::DrawProgressBar(const int x,
                             const int y,
                             const int width,
                             const int height,
                             const uint16_t percentageX100)
{
    for (int pixelX = x; pixelX < x + width; ++pixelX)
    {
        display_.DrawPixel(pixelX, y);
        display_.DrawPixel(pixelX, y + height - 1);
    }
    for (int pixelY = y; pixelY < y + height; ++pixelY)
    {
        display_.DrawPixel(x, pixelY);
        display_.DrawPixel(x + width - 1, pixelY);
    }

    const int innerWidth = width - 2;
    const int filledWidth =
        innerWidth * std::min<uint16_t>(percentageX100, 10000U) / 10000;
    for (int pixelX = 0; pixelX < filledWidth; ++pixelX)
    {
        for (int pixelY = 1; pixelY < height - 1; ++pixelY)
        {
            display_.DrawPixel(x + 1 + pixelX, y + pixelY);
        }
    }
}

void OledUi::DrawWaitingScreen()
{
    display_.DrawText(22, 18, "PC MONITOR");
    display_.DrawText(16, 38, "watting for link");
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
        std::string displayName = ToDisplayText(snapshot.gpuNames[gpuIndex].data());
        if (displayName.empty())
        {
            displayName = "N/A";
        }
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
