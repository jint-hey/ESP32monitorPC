#include "oled_display.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "app_config.hpp"
#include "oled_font_6x8.hpp"

namespace
{
constexpr std::array<uint8_t, 28> INITIALIZATION_COMMANDS = {
    0xAE, 0x00, 0x10, 0x40, 0x81, 0xCF, 0xA1, 0xC8,
    0xA6, 0xA8, 0x3F, 0xD3, 0x00, 0xD5, 0x80, 0xD9,
    0xF1, 0xDA, 0x12, 0xDB, 0x40, 0x20, 0x02, 0x8D,
    0x14, 0xA4, 0xA6, 0xAF,
};
}

esp_err_t OledDisplay::Initialize()
{
    const i2c_master_bus_config_t busConfiguration = {
        .i2c_port = app_config::OLED_I2C_PORT,
        .sda_io_num = app_config::OLED_SDA_GPIO,
        .scl_io_num = app_config::OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t result = i2c_new_master_bus(&busConfiguration, &busHandle_);
    if (result != ESP_OK)
    {
        return result;
    }

    const i2c_device_config_t deviceConfiguration = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = app_config::OLED_I2C_ADDRESS,
        .scl_speed_hz = app_config::OLED_I2C_FREQUENCY_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };

    result = i2c_master_bus_add_device(busHandle_, &deviceConfiguration, &deviceHandle_);
    if (result != ESP_OK)
    {
        i2c_del_master_bus(busHandle_);
        busHandle_ = nullptr;
        return result;
    }

    for (const uint8_t command : INITIALIZATION_COMMANDS)
    {
        result = WriteCommand(command);
        if (result != ESP_OK)
        {
            return result;
        }
    }

    Clear();
    return Refresh();
}

esp_err_t OledDisplay::WriteCommand(const uint8_t command)
{
    const uint8_t data[] = {0x00, command};
    return i2c_master_transmit(deviceHandle_, data, sizeof(data), 1000);
}

esp_err_t OledDisplay::Refresh()
{
    std::array<uint8_t, WIDTH + 1> pageData{};
    pageData[0] = 0x40;

    for (int page = 0; page < HEIGHT / 8; ++page)
    {
        esp_err_t result = WriteCommand(static_cast<uint8_t>(0xB0 + page));
        if (result == ESP_OK)
        {
            result = WriteCommand(0x00);
        }
        if (result == ESP_OK)
        {
            result = WriteCommand(0x10);
        }
        if (result != ESP_OK)
        {
            return result;
        }

        std::memcpy(pageData.data() + 1,
                    framebuffer_.data() + page * WIDTH,
                    WIDTH);
        result = i2c_master_transmit(deviceHandle_,
                                     pageData.data(),
                                     pageData.size(),
                                     1000);
        if (result != ESP_OK)
        {
            return result;
        }
    }
    return ESP_OK;
}

void OledDisplay::Clear()
{
    framebuffer_.fill(0);
}

void OledDisplay::DrawPixel(const int x, const int y, const bool enabled)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
    {
        return;
    }

    uint8_t &cell = framebuffer_[static_cast<size_t>(y / 8) * WIDTH + x];
    const uint8_t mask = static_cast<uint8_t>(1U << (y % 8));
    if (enabled)
    {
        cell |= mask;
    }
    else
    {
        cell &= static_cast<uint8_t>(~mask);
    }
}

void OledDisplay::DrawCharacterClipped(const int x,
                                       const int y,
                                       char character,
                                       const int clipStart,
                                       const int clipEnd)
{
    const auto value = static_cast<unsigned char>(character);
    if (value < 32 || value > 126)
    {
        character = '?';
    }

    const uint8_t *glyph = oled_font::ASCII_6X8[static_cast<unsigned char>(character) - 32];
    for (int column = 0; column < CHARACTER_WIDTH; ++column)
    {
        const int pixelX = x + column;
        if (pixelX < clipStart || pixelX >= clipEnd)
        {
            continue;
        }
        for (int row = 0; row < CHARACTER_HEIGHT; ++row)
        {
            if ((glyph[column] & (1U << row)) != 0)
            {
                DrawPixel(pixelX, y + row);
            }
        }
    }
}

void OledDisplay::DrawText(const int x, const int y, const std::string_view text)
{
    DrawTextClipped(x, y, text, 0, WIDTH);
}

void OledDisplay::DrawTextClipped(int x,
                                  const int y,
                                  const std::string_view text,
                                  const int clipX,
                                  const int clipWidth)
{
    if (clipWidth <= 0)
    {
        return;
    }

    const int clipStart = std::max(0, clipX);
    const int clipEnd = std::min(WIDTH, clipX + clipWidth);
    for (const char character : text)
    {
        if (x >= clipEnd)
        {
            break;
        }
        DrawCharacterClipped(x, y, character, clipStart, clipEnd);
        x += CHARACTER_WIDTH;
    }
}
