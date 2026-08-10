#include "oled_ui.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "app_config.hpp"
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
               CodexQuotaStateStore& codexQuotaState)
    : display_(display),
      hardwareState_(hardwareState),
      codexQuotaState_(codexQuotaState)
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

        if ((nowMs / PAGE_SWITCH_PERIOD_MS) % 2U == 0U)
        {
            const HardwareSnapshot snapshot = hardwareState_.Copy();
            if (snapshot.hasUsage)
            {
                DrawDashboard(snapshot, nowMs);
            }
            else
            {
                DrawWaitingScreen();
            }
        }
        else
        {
            DrawCodexPage(codexQuotaState_.Copy());
        }

        display_.Refresh();
        vTaskDelay(UI_REFRESH_PERIOD);
    }
}

void OledUi::DrawCodexPage(const CodexQuotaSnapshot& snapshot)
{
    display_.DrawText(31, 0, "CODEX QUOTA");

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

    DrawCodexWindow(10, 19, 'P', snapshot.primary);
    DrawCodexWindow(31, 40, 'S', snapshot.secondary);

    if (snapshot.rateLimitReached)
    {
        display_.DrawText(22, 54, "RATE LIMITED");
    }
    else
    {
        std::array<char, 22> footer{};
        std::snprintf(footer.data(), footer.size(), "UPDATE %lu",
                      static_cast<unsigned long>(snapshot.updateCount));
        display_.DrawText(37, 54, footer.data());
    }
}

void OledUi::DrawCodexWindow(const int textY,
                             const int barY,
                             const char label,
                             const CodexQuotaWindow& window)
{
    // The longest formatted line needs 25 visible characters plus '\0'.
    // DrawTextClipped still limits rendering to the 126-pixel viewport.
    std::array<char, 32> text{};
    if (!window.valid)
    {
        std::snprintf(text.data(), text.size(), "%c WINDOW N/A", label);
        display_.DrawText(1, textY, text.data());
        DrawProgressBar(1, barY, 126, 7, 0);
        return;
    }

    std::array<char, 10> duration{};
    FormatWindowDuration(window.windowDurationMinutes, duration.data(), duration.size());
    std::snprintf(text.data(), text.size(), "%c%s U%u.%u R%u.%u",
                  label,
                  duration.data(),
                  window.usedPercentX100 / 100U,
                  (window.usedPercentX100 % 100U) / 10U,
                  window.remainingPercentX100 / 100U,
                  (window.remainingPercentX100 % 100U) / 10U);
    display_.DrawTextClipped(1, textY, text.data(), 1, 126);
    DrawProgressBar(1, barY, 126, 7, window.usedPercentX100);
}

void OledUi::DrawProgressBar(const int x,
                             const int y,
                             const int width,
                             const int height,
                             const uint16_t usedX100)
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
    const int filledWidth = innerWidth * std::min<uint16_t>(usedX100, 10000U) / 10000;
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
