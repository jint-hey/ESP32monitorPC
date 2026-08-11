#include "codex_quota_state.hpp"
#include "hardware_state.hpp"
#include "oled_display.hpp"
#include "oled_ui.hpp"
#include "pc_connection_state.hpp"
#include "uart_receiver.hpp"

extern "C" void app_main()
{
    static HardwareStateStore hardwareState;
    static CodexQuotaStateStore codexQuotaState;
    static PcConnectionStateStore connectionState;
    static OledDisplay display;
    static UartReceiver uartReceiver(hardwareState, codexQuotaState, connectionState);
    static OledUi ui(display, hardwareState, codexQuotaState, connectionState);

    if (display.Initialize() != ESP_OK)
    {
        return;
    }

    if (ui.Start() != ESP_OK)
    {
        return;
    }

    uartReceiver.Start();
}
