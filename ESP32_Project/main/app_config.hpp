#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/uart.h"

namespace app_config
{
    inline constexpr uart_port_t UART_PORT = UART_NUM_0;
    inline constexpr int UART_BAUD_RATE = 115200;
    inline constexpr int UART_TX_GPIO = 43;
    inline constexpr int UART_RX_GPIO = 44;
    inline constexpr int UART_DRIVER_RX_BUFFER_SIZE = 2048;
    inline constexpr int UART_DRIVER_TX_BUFFER_SIZE = 1024;

    inline constexpr i2c_port_t OLED_I2C_PORT = I2C_NUM_1;
    inline constexpr gpio_num_t OLED_SDA_GPIO = GPIO_NUM_13;
    inline constexpr gpio_num_t OLED_SCL_GPIO = GPIO_NUM_14;
    inline constexpr uint32_t OLED_I2C_FREQUENCY_HZ = 400000;
    inline constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;

    // Hardware monitor and Codex quota page rotation interval.
    inline constexpr uint32_t OLED_PAGE_SWITCH_SECONDS = 5;

    // Fixed timezone used to render the Codex reset timestamp (UTC+8).
    inline constexpr int32_t CODEX_TIMEZONE_OFFSET_MINUTES = 480;
}
