// SPDX-License-Identifier: Apache-2.0

#include <idfxx/gpio>
#include <idfxx/lcd/ili9341>
#include <idfxx/lcd/panel_io>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <esp_lcd_panel_ops.h>
#include <vector>

using namespace std::chrono_literals;
using namespace frequency_literals;

static constexpr idfxx::log::logger logger{"example"};

// Pin assignments — change to match your board.
static constexpr auto PIN_MOSI = idfxx::gpio_11;
static constexpr auto PIN_SCLK = idfxx::gpio_12;
static constexpr auto PIN_CS = idfxx::gpio_10;
static constexpr auto PIN_DC = idfxx::gpio_9;
static constexpr auto PIN_RST = idfxx::gpio_14;
static constexpr auto PIN_BL = idfxx::gpio_15;

static constexpr size_t DISPLAY_W = 240;
static constexpr size_t DISPLAY_H = 320;
static constexpr size_t BAND_H = 40;

// RGB565 packed and byte-swapped for big-endian panel data.
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t v = static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

extern "C" void app_main() {
    try {
        // --- Backlight on ---
        idfxx::gpio backlight = PIN_BL;
        idfxx::configure_gpios({.mode = idfxx::gpio::mode::output}, backlight);
        backlight.set_level(idfxx::gpio::level::high);

        // --- SPI bus ---
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.sclk = PIN_SCLK;
        bus_cfg.max_transfer_sz = DISPLAY_W * BAND_H * sizeof(uint16_t);
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

        // --- Panel I/O ---
        idfxx::lcd::panel_io io(
            bus,
            {
                .cs_gpio = PIN_CS,
                .dc_gpio = PIN_DC,
                .spi_mode = 0,
                .pclk_freq = 40_MHz,
                .trans_queue_depth = 10,
                .lcd_cmd_bits = 8,
                .lcd_param_bits = 8,
            }
        );

        // --- ILI9341 panel ---
        idfxx::lcd::ili9341 panel(
            io,
            {
                .reset_gpio = PIN_RST,
                .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
                .bits_per_pixel = 16,
            }
        );

        panel.display_on(true);
        logger.info("Display initialized ({}x{})", DISPLAY_W, DISPLAY_H);

        // --- Draw rainbow bands ---
        constexpr std::array<uint16_t, DISPLAY_H / BAND_H> colors = {
            rgb565(255, 0, 0),     // red
            rgb565(255, 127, 0),   // orange
            rgb565(255, 255, 0),   // yellow
            rgb565(0, 255, 0),     // green
            rgb565(0, 127, 255),   // azure
            rgb565(0, 0, 255),     // blue
            rgb565(148, 0, 211),   // violet
            rgb565(255, 255, 255), // white
        };

        std::vector<uint16_t> band(DISPLAY_W * BAND_H);
        auto* handle = panel.idf_handle();

        for (size_t i = 0; i < colors.size(); ++i) {
            std::ranges::fill(band, colors[i]);
            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(handle, 0, i * BAND_H, DISPLAY_W, (i + 1) * BAND_H, band.data()));
        }

        logger.info("Drew {} color bands", colors.size());

    } catch (const std::system_error& e) {
        logger.error("LCD error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
