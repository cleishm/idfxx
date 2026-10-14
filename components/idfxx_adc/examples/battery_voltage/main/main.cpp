// SPDX-License-Identifier: Apache-2.0

// Reads a battery voltage through a 2:1 resistor divider every few seconds.

#include <idfxx/adc>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// ADC1 pin wired to the divider midpoint (change as needed).
// The ADC1 pins differ by target, so this picks the lowest-numbered ADC1 pin
// that isn't a strapping, flash, USB, JTAG, console UART or 32 kHz crystal pin.
#if CONFIG_IDF_TARGET_ESP32
static constexpr auto battery_pin = idfxx::gpio_34;
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32H2
static constexpr auto battery_pin = idfxx::gpio_1;
#elif CONFIG_IDF_TARGET_ESP32C3
static constexpr auto battery_pin = idfxx::gpio_3;
#elif CONFIG_IDF_TARGET_ESP32C6
static constexpr auto battery_pin = idfxx::gpio_2;
#else
static constexpr auto battery_pin = idfxx::gpio_3;
#endif

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
