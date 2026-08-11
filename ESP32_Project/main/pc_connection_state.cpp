#include "pc_connection_state.hpp"

#include "app_config.hpp"

#include "freertos/task.h"

PcConnectionStateStore::PcConnectionStateStore()
{
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_ != nullptr);
}

void PcConnectionStateStore::MarkPacketReceived()
{
    const TickType_t now = xTaskGetTickCount();

    xSemaphoreTake(mutex_, portMAX_DELAY);
    lastPacketTick_ = now;
    connected_ = true;
    xSemaphoreGive(mutex_);
}

bool PcConnectionStateStore::MarkDisconnectedIfTimedOut()
{
    const TickType_t now = xTaskGetTickCount();
    const TickType_t timeoutTicks =
        pdMS_TO_TICKS(app_config::PC_CONNECTION_TIMEOUT_MS);
    bool disconnected = false;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    if (connected_)
    {
        // Unsigned subtraction remains valid when the FreeRTOS tick wraps.
        const TickType_t elapsedTicks =
            static_cast<TickType_t>(now - lastPacketTick_);

        if (elapsedTicks >= timeoutTicks)
        {
            connected_ = false;
            disconnected = true;
        }
    }

    xSemaphoreGive(mutex_);
    return disconnected;
}

bool PcConnectionStateStore::IsConnected() const
{
    bool connected = false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    connected = connected_;
    xSemaphoreGive(mutex_);

    return connected;
}
