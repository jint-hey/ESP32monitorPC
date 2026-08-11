#pragma once

#include "codex_quota_state.hpp"
#include "hardware_state.hpp"
#include "oled_display.hpp"
#include "pc_connection_state.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

class OledUi
{
public:
    OledUi(OledDisplay& display,
           HardwareStateStore& hardwareState,
           CodexQuotaStateStore& codexQuotaState,
           PcConnectionStateStore& connectionState);

    esp_err_t Start();

private:
    static void TaskEntry(void* context);
    void TaskLoop();
    void DrawWaitingScreen();
    void DrawDashboard(const HardwareSnapshot& snapshot, uint64_t nowMs);
    void DrawCodexPage(const CodexQuotaSnapshot& snapshot);
    void DrawProgressBar(int x,
                         int y,
                         int width,
                         int height,
                         uint16_t percentageX100);
    void DrawUsageRow(int y,
                      std::string_view label,
                      std::string_view name,
                      uint16_t usageX100,
                      uint64_t nowMs,
                      std::size_t rowIndex);

    static std::string ToDisplayText(const char* utf8Text);
    static int ScrollOffset(std::size_t characterCount,
                            int viewportWidth,
                            uint64_t nowMs,
                            std::size_t rowIndex);

    OledDisplay& display_;
    HardwareStateStore& hardwareState_;
    CodexQuotaStateStore& codexQuotaState_;
    PcConnectionStateStore& connectionState_;
    TaskHandle_t task_ = nullptr;
};
