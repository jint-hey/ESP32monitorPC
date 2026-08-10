#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "driver/i2c_master.h"
#include "esp_err.h"

class OledDisplay
{
public:
    static constexpr int WIDTH = 128;
    static constexpr int HEIGHT = 64;
    static constexpr int CHARACTER_WIDTH = 6;
    static constexpr int CHARACTER_HEIGHT = 8;

    esp_err_t Initialize();
    esp_err_t Refresh();

    void Clear();
    void DrawPixel(int x, int y, bool enabled = true);
    void DrawText(int x, int y, std::string_view text);
    void DrawTextClipped(int x, int y, std::string_view text, int clipX, int clipWidth);

private:
    esp_err_t WriteCommand(uint8_t command);
    void DrawCharacterClipped(int x, int y, char character, int clipStart, int clipEnd);

    i2c_master_bus_handle_t busHandle_ = nullptr;
    i2c_master_dev_handle_t deviceHandle_ = nullptr;
    std::array<uint8_t, WIDTH * HEIGHT / 8> framebuffer_{};
};
