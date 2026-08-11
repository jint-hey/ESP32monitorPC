#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class PcConnectionStateStore
{
public:
    PcConnectionStateStore();

    PcConnectionStateStore(const PcConnectionStateStore&) = delete;
    PcConnectionStateStore& operator=(const PcConnectionStateStore&) = delete;

    void MarkPacketReceived();

    // Returns true only for the connected-to-disconnected transition.
    bool MarkDisconnectedIfTimedOut();

    bool IsConnected() const;

private:
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TickType_t lastPacketTick_ = 0;
    bool connected_ = false;
};
