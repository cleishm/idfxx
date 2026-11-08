// SPDX-License-Identifier: Apache-2.0

// Only sets up this board's display; you don't need to read this to follow the example.

#pragma once

#include <idfxx/epaper/el133uf1>
#include <idfxx/gpio>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <chrono>

namespace display_setup {

using namespace std::chrono_literals;

// The reTerminal E1004's display wiring.
inline constexpr auto PIN_SCLK = idfxx::gpio_7;
inline constexpr auto PIN_MOSI = idfxx::gpio_9;
inline constexpr auto PIN_CS = idfxx::gpio_10;
inline constexpr auto PIN_CS1 = idfxx::gpio_2;
inline constexpr auto PIN_DC = idfxx::gpio_11;
inline constexpr auto PIN_RST = idfxx::gpio_38;
inline constexpr auto PIN_BUSY = idfxx::gpio_13;
inline constexpr auto PIN_POWER = idfxx::gpio_12; // display power enable, active high

// Everything the display needs to stay alive. The members are declared parents first, so they are destroyed
// children first.
struct board {
    board()
        : power(_power_on())
        , bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, _bus_config())
        , panel(
              bus,
              {
                  .master_cs_gpio = PIN_CS,
                  .slave_cs_gpio = PIN_CS1,
                  .dc_gpio = PIN_DC,
                  .reset_gpio = PIN_RST,
                  .busy_gpio = PIN_BUSY,
              }
          ) {}

    board(const board&) = delete;
    board& operator=(const board&) = delete;

    idfxx::gpio power;
    idfxx::spi::master_bus bus;
    idfxx::epaper::el133uf1 panel;

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
