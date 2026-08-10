#include "hardware_state.hpp"
#include "oled_display.hpp"
#include "oled_ui.hpp"
#include "uart_receiver.hpp"

extern "C" void app_main()
{
    static HardwareStateStore hardwareState;
    static OledDisplay display;
    static UartReceiver uartReceiver(hardwareState);
    static OledUi ui(display, hardwareState);

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
