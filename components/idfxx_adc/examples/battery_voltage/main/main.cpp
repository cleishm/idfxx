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

extern "C" void app_main() {
    // Battery → R1 → pin → R2 → GND with R1 == R2 halves the voltage at the
    // pin; the 2:1 divider config makes read_voltage report the battery side.
    idfxx::adc::input battery({.pin = battery_pin, .divider = {2, 1}});
    logger.info("ADC on GPIO {} ({}calibrated)", battery.pin().num(), battery.calibrated() ? "" : "un");

    while (true) {
        auto v = battery.try_read_voltage();
        if (v) {
            logger.info("battery {}", *v);
        } else {
            logger.warn("read failed: {}", v.error().message());
        }
        idfxx::delay(5s);
    }
}
