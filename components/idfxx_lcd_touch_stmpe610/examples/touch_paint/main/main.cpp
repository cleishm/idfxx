// SPDX-License-Identifier: Apache-2.0

#include <idfxx/gpio>
#include <idfxx/lcd/ili9341>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/stmpe610>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch.h>
#include <vector>

using namespace std::chrono_literals;
using namespace frequency_literals;

static constexpr idfxx::log::logger logger{"example"};

// Pin assignments — change to match your board.
static constexpr auto PIN_MOSI = idfxx::gpio_11;
static constexpr auto PIN_MISO = idfxx::gpio_13;
static constexpr auto PIN_SCLK = idfxx::gpio_12;
static constexpr auto PIN_LCD_CS = idfxx::gpio_10;
static constexpr auto PIN_LCD_DC = idfxx::gpio_9;
static constexpr auto PIN_LCD_RST = idfxx::gpio_14;
static constexpr auto PIN_BL = idfxx::gpio_15;
static constexpr auto PIN_TOUCH_CS = idfxx::gpio_16;

static constexpr int DISPLAY_W = 240;
static constexpr int DISPLAY_H = 320;
static constexpr int BAND_H = 40;
static constexpr int DOT_SIZE = 6;

// RGB565 packed and byte-swapped for big-endian panel data.
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t v = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

static constexpr uint16_t COLOR_BG = rgb565(0, 0, 0);
static constexpr uint16_t COLOR_FG = rgb565(255, 255, 255);

static void clear_screen(esp_lcd_panel_handle_t panel) {
    std::vector<uint16_t> band(DISPLAY_W * BAND_H, COLOR_BG);
    for (int y = 0; y < DISPLAY_H; y += BAND_H) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, DISPLAY_W, y + BAND_H, band.data()));
    }
}

static void draw_dot(esp_lcd_panel_handle_t panel, uint16_t cx, uint16_t cy, uint16_t color) {
    int x1 = std::clamp(static_cast<int>(cx) - DOT_SIZE / 2, 0, DISPLAY_W - DOT_SIZE);
    int y1 = std::clamp(static_cast<int>(cy) - DOT_SIZE / 2, 0, DISPLAY_H - DOT_SIZE);
    std::array<uint16_t, DOT_SIZE * DOT_SIZE> buf;
    buf.fill(color);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x1, y1, x1 + DOT_SIZE, y1 + DOT_SIZE, buf.data()));
}

extern "C" void app_main() {
    try {
        // --- Backlight on ---
        idfxx::gpio backlight = PIN_BL;
        idfxx::configure_gpios({.mode = idfxx::gpio::mode::output}, backlight);
        backlight.set_level(idfxx::gpio::level::high);

        // --- Shared SPI bus ---
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.miso = PIN_MISO;
        bus_cfg.sclk = PIN_SCLK;
        bus_cfg.max_transfer_sz = DISPLAY_W * BAND_H * sizeof(uint16_t);
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

        // --- Display (ILI9341) on LCD_CS, 40 MHz ---
        idfxx::lcd::panel_io lcd_io(
            bus,
            {
                .cs_gpio = PIN_LCD_CS,
                .dc_gpio = PIN_LCD_DC,
                .spi_mode = 0,
                .pclk_freq = 40_MHz,
                .trans_queue_depth = 10,
                .lcd_cmd_bits = 8,
                .lcd_param_bits = 8,
            }
        );
        idfxx::lcd::ili9341 panel(
            lcd_io,
            {
                .reset_gpio = PIN_LCD_RST,
                .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
                .bits_per_pixel = 16,
            }
        );
        panel.display_on(true);

        // --- Touch (STMPE610) on TOUCH_CS, 1 MHz ---
        idfxx::lcd::panel_io touch_io(
            bus,
            {
                .cs_gpio = PIN_TOUCH_CS,
                .dc_gpio = idfxx::gpio::nc(),
                .spi_mode = 0,
                .pclk_freq = 1_MHz,
                .trans_queue_depth = 10,
                .lcd_cmd_bits = 8,
                .lcd_param_bits = 8,
            }
        );
        idfxx::lcd::stmpe610 touch(
            touch_io,
            {
                .x_max = DISPLAY_W,
                .y_max = DISPLAY_H,
            }
        );

        auto* lcd_handle = panel.idf_handle();
        auto* touch_handle = touch.idf_handle();

        clear_screen(lcd_handle);
        logger.info("Touch the display to paint");

        // --- Poll loop (skip redraws when the reported point hasn't moved) ---
        uint16_t last_x = 0, last_y = 0;
        bool have_last = false;
        while (true) {
            if (esp_lcd_touch_read_data(touch_handle) == ESP_OK) {
                std::array<esp_lcd_touch_point_data_t, 1> data{};
                uint8_t count = 0;
                if (esp_lcd_touch_get_data(touch_handle, data.data(), &count, 1) == ESP_OK && count > 0) {
                    if (!have_last || data[0].x != last_x || data[0].y != last_y) {
                        draw_dot(lcd_handle, data[0].x, data[0].y, COLOR_FG);
                        last_x = data[0].x;
                        last_y = data[0].y;
                        have_last = true;
                    }
                } else {
                    have_last = false;
                }
            }
            idfxx::delay(20ms);
        }

    } catch (const std::system_error& e) {
        logger.error("Touch paint error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
