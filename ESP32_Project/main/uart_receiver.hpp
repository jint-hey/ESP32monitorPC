#pragma once

#include "codex_quota_state.hpp"
#include "hardware_state.hpp"
#include "pc_connection_state.hpp"
#include "pc_protocol.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstdint>

class UartReceiver
{
public:
    explicit UartReceiver(
        HardwareStateStore& hardwareState,
        CodexQuotaStateStore& codexQuotaState,
        PcConnectionStateStore& connectionState
    );

    esp_err_t Start();

private:
    static void TaskEntry(void* context);
    void TaskLoop();
    bool HandlePacket(
        const pc_protocol::Packet& packet
    );
    void SendPong(uint16_t sequence);

    HardwareStateStore& hardwareState_;
    CodexQuotaStateStore& codexQuotaState_;
    PcConnectionStateStore& connectionState_;
    pc_protocol::Parser parser_;
    TaskHandle_t task_ = nullptr;
    std::array<uint8_t, 256> readBuffer_{};
    std::array<uint8_t, pc_protocol::PACKET_OVERHEAD> pongBuffer_{};
};
