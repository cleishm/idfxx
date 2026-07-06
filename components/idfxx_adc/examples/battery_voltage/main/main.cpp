// SPDX-License-Identifier: Apache-2.0

// Reads a battery voltage through a 2:1 resistor divider every few seconds.

#include <idfxx/adc>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// ADC-capable pin wired to the divider midpoint (change as needed).
static constexpr auto battery_pin = idfxx::gpio_3;

// Divider ratio: battery → R1 → pin → R2 → GND with R1 == R2 doubles.
static constexpr int divider_num = 2;
static constexpr int divider_den = 1;

extern "C" void app_main() {
    idfxx::adc::input battery({.pin = battery_pin});
    logger.info("ADC on GPIO {} ({}calibrated)", battery.pin().num(), battery.calibrated() ? "" : "un");

    while (true) {
        auto v = battery.try_read_voltage();
        if (v) {
            logger.info("pin {}, battery {}", *v, *v * divider_num / divider_den);
        } else {
            logger.warn("read failed: {}", v.error().message());
        }
        idfxx::delay(5s);
    }
}
