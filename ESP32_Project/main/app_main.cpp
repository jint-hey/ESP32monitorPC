#include "codex_quota_state.hpp"
#include "hardware_state.hpp"
#include "oled_display.hpp"
#include "oled_ui.hpp"
#include "pc_connection_state.hpp"
#include "uart_receiver.hpp"
#include "router_monitor_service.hpp"
#include "router_monitor_status.hpp"

extern "C" void app_main()
{
    static HardwareStateStore hardwareState;
    static CodexQuotaStateStore codexQuotaState;
    static PcConnectionStateStore connectionState;
    static router_monitor::RouterMonitorStatusStore routerMonitorState;
    static OledDisplay display;
    static UartReceiver uartReceiver(hardwareState, codexQuotaState, connectionState);
    static OledUi ui(display, hardwareState, codexQuotaState, connectionState,
                     routerMonitorState);
    static router_monitor::RouterMonitorService routerMonitor(routerMonitorState);

    if (display.Initialize() != ESP_OK)
    {
        return;
    }

    if (ui.Start() != ESP_OK)
    {
        return;
    }

    uartReceiver.Start();
    // Runs independently at a lower priority than UART and OLED tasks.
    // A router-monitor startup failure must not stop the original PC monitor.
    if (routerMonitor.Start() != ESP_OK)
    {
        routerMonitorState.UpdateError("TASK START FAILED");
    }
}
