#include "uart_receiver.hpp"

#include "app_config.hpp"

#include "driver/uart.h"

UartReceiver::UartReceiver(
    HardwareStateStore& hardwareState,
    CodexQuotaStateStore& codexQuotaState
)
    : hardwareState_(hardwareState),
      codexQuotaState_(codexQuotaState)
{}

esp_err_t UartReceiver::Start()
{
    if (task_ != nullptr)
    {
        return ESP_OK;
    }

    if (uart_is_driver_installed(
        app_config::UART_PORT))
    {
        const esp_err_t deleteResult =
            uart_driver_delete(
                app_config::UART_PORT
            );

        if (deleteResult != ESP_OK)
        {
            return deleteResult;
        }
    }

    uart_config_t configuration{};
    configuration.baud_rate = app_config::UART_BAUD_RATE;
    configuration.data_bits = UART_DATA_8_BITS;
    configuration.parity = UART_PARITY_DISABLE;
    configuration.stop_bits = UART_STOP_BITS_1;
    configuration.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    configuration.rx_flow_ctrl_thresh = 0;
    configuration.source_clk = UART_SCLK_DEFAULT;

    esp_err_t result = uart_param_config(
        app_config::UART_PORT,
        &configuration
    );

    if (result != ESP_OK)
    {
        return result;
    }

    result = uart_set_pin(
        app_config::UART_PORT,
        app_config::UART_TX_GPIO,
        app_config::UART_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    if (result != ESP_OK)
    {
        return result;
    }

    result = uart_driver_install(
        app_config::UART_PORT,
        app_config::UART_DRIVER_RX_BUFFER_SIZE,
        app_config::UART_DRIVER_TX_BUFFER_SIZE,
        0,
        nullptr,
        0
    );

    if (result != ESP_OK)
    {
        return result;
    }

    uart_flush_input(app_config::UART_PORT);

    const BaseType_t taskResult = xTaskCreate(
        TaskEntry,
        "pc_uart_rx",
        4096,
        this,
        10,
        &task_
    );

    if (taskResult != pdPASS)
    {
        uart_driver_delete(app_config::UART_PORT);
        task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void UartReceiver::TaskEntry(void* context)
{
    static_cast<UartReceiver*>(context)->TaskLoop();
}

void UartReceiver::TaskLoop()
{
    while (true)
    {
        const int bytesRead = uart_read_bytes(
            app_config::UART_PORT,
            readBuffer_.data(),
            readBuffer_.size(),
            pdMS_TO_TICKS(20)
        );

        if (bytesRead > 0)
        {
            parser_.PushBytes(
                readBuffer_.data(),
                static_cast<std::size_t>(bytesRead)
            );
        }

        pc_protocol::Packet packet;

        while (parser_.TryGetPacket(packet))
        {
            HandlePacket(packet);
        }
    }
}

void UartReceiver::HandlePacket(
    const pc_protocol::Packet& packet
)
{
    if (packet.type == pc_protocol::TYPE_PING)
    {
        SendPong(packet.sequence);
        return;
    }

    if (packet.type == pc_protocol::TYPE_CODEX_QUOTA)
    {
        codexQuotaState_.ApplyPacket(packet);
        return;
    }

    hardwareState_.ApplyPacket(packet);
}

void UartReceiver::SendPong(uint16_t sequence)
{
    const std::size_t length =
        pc_protocol::EncodePacket(
            pc_protocol::TYPE_PONG,
            0,
            sequence,
            nullptr,
            0,
            pongBuffer_.data(),
            pongBuffer_.size()
        );

    if (length > 0)
    {
        uart_write_bytes(
            app_config::UART_PORT,
            pongBuffer_.data(),
            length
        );
    }
}
