// SPDX-License-Identifier: Apache-2.0

// Only sets up this board's display; you don't need to read this to follow the example.

#pragma once

#include <idfxx/gpio>
#include <idfxx/lcd/color>
#include <idfxx/lcd/ili9341>
#include <idfxx/panel_io>
#include <idfxx/spi/master>

#include <cstddef>

namespace display_setup {

using namespace quanta::frequency_literals;

// Pin assignments — change to match your board.
inline constexpr auto PIN_MOSI = idfxx::gpio_11;
inline constexpr auto PIN_SCLK = idfxx::gpio_12;
inline constexpr auto PIN_CS = idfxx::gpio_10;
inline constexpr auto PIN_DC = idfxx::gpio_9;
inline constexpr auto PIN_RST = idfxx::gpio_14;
inline constexpr auto PIN_BL = idfxx::gpio_15;

// The panel's native portrait layout is swapped to landscape by the example.
inline constexpr size_t DISPLAY_W = 320;

// Everything the display needs to stay alive. The members are declared parents first, so they are destroyed
// children first.
struct board {
    // band_rows is the number of rows the example writes to the panel at a time.
    explicit board(size_t band_rows)
        : backlight(_backlight_on())
        , bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, _bus_config(band_rows))
        , io(bus,
             {
                 .cs_gpio = PIN_CS,
                 .dc_gpio = PIN_DC,
                 .spi_mode = 0,
                 .pclk_freq = 40_MHz,
                 .trans_queue_depth = 10,
                 .lcd_cmd_bits = 8,
                 .lcd_param_bits = 8,
             })
        , panel(
              io,
              {
                  .reset_gpio = PIN_RST,
                  .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
                  .bits_per_pixel = 16,
              }
          ) {}

    board(const board&) = delete;
    board& operator=(const board&) = delete;

    idfxx::gpio backlight;
    idfxx::spi::master_bus bus;
    idfxx::panel_io io;
    idfxx::lcd::ili9341 panel;

private:
    // --- Backlight on ---
    static idfxx::gpio _backlight_on() {
        idfxx::gpio backlight = PIN_BL;
        idfxx::configure_gpios({.mode = idfxx::gpio::mode::output}, backlight);
        backlight.set_level(idfxx::gpio::level::high);
        return backlight;
    }

    // --- SPI bus ---
    static idfxx::spi::bus_config _bus_config(size_t band_rows) {
        // The largest single draw is one band of the full width.
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.sclk = PIN_SCLK;
        bus_cfg.max_transfer_sz = DISPLAY_W * band_rows * sizeof(idfxx::lcd::rgb565);
        return bus_cfg;
    }
};

} // namespace display_setup
