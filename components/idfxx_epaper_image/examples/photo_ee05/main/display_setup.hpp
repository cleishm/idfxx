// SPDX-License-Identifier: Apache-2.0

// Only sets up this board's display; you don't need to read this to follow the example.

#pragma once

#include <idfxx/epaper/uc8179>
#include <idfxx/gpio>
#include <idfxx/panel_io>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <chrono>

namespace display_setup {

using namespace std::chrono_literals;

// The XIAO EE05's display wiring.
inline constexpr auto PIN_SCLK = idfxx::gpio_7;
inline constexpr auto PIN_MOSI = idfxx::gpio_9;
inline constexpr auto PIN_CS = idfxx::gpio_44;
inline constexpr auto PIN_DC = idfxx::gpio_10;
inline constexpr auto PIN_RST = idfxx::gpio_38;
inline constexpr auto PIN_BUSY = idfxx::gpio_4;
inline constexpr auto PIN_POWER = idfxx::gpio_43; // display power enable, active high

// Everything the display needs to stay alive. The members are declared parents first, so they are destroyed
// children first.
struct board {
    board()
        : power(_power_on())
        , bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, _bus_config())
        , io(bus, idfxx::epaper::uc8179::spi_io_config(PIN_CS, PIN_DC))
        , panel(
              io,
              {
                  .reset_gpio = PIN_RST,
                  .busy_gpio = PIN_BUSY,
              }
          ) {}

    board(const board&) = delete;
    board& operator=(const board&) = delete;

    idfxx::gpio power;
    idfxx::spi::master_bus bus;
    idfxx::panel_io io;
    // The calibration holds at the driver's default gray frame rate and source voltage.
    idfxx::epaper::uc8179 panel;

private:
    // --- Display power rail ---
    static idfxx::gpio _power_on() {
        idfxx::gpio power = PIN_POWER;
        power.set_direction(idfxx::gpio::mode::output);
        power.set_level(idfxx::gpio::level::high);
        idfxx::delay(100ms);
        return power;
    }

    // --- SPI bus ---
    static idfxx::spi::bus_config _bus_config() {
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.sclk = PIN_SCLK;
        return bus_cfg;
    }
};

} // namespace display_setup
