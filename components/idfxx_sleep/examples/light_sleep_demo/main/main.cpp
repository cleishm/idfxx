// SPDX-License-Identifier: Apache-2.0

// Periodic light sleep with timer and button wake-up.
//
// The chip light-sleeps until either a timer expires or the BOOT button is
// pressed, then reports what woke it. Execution resumes in place after each
// wake — all state is retained across light sleep.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/sleep>

#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"lightsleep"};

// BOOT button: pulled low while pressed. It is on GPIO 9 on the ESP32-C3,
// ESP32-C6 and ESP32-H2 devkits, and on GPIO 0 on the others.
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
static constexpr auto WAKE_PIN = idfxx::gpio_9;
#else
static constexpr auto WAKE_PIN = idfxx::gpio_0;
#endif

// How long to sleep when the button stays untouched.
static constexpr auto SLEEP_INTERVAL = 10s;

extern "C" void app_main() {
    logger.info("=== Light Sleep Demo ===");

    // The wake pin must be an input. Most boards strap BOOT with an external
    // pull-up; enable the internal one to cover the rest.
    idfxx::gpio pin = WAKE_PIN;
    pin.set_direction(idfxx::gpio::mode::input);
    pin.set_pull_mode(idfxx::gpio::pull_mode::pullup);

    for (int cycle = 1;; ++cycle) {
        logger.info("cycle {}: entering light sleep (timer {}, or press BOOT)", cycle, SLEEP_INTERVAL);

        // GPIO wake-up is level-triggered: a held button wakes the chip
        // immediately.
        const auto cause = idfxx::sleep::light_sleep(
            idfxx::sleep::timer_wake{SLEEP_INTERVAL}, idfxx::sleep::gpio_wake{pin, idfxx::gpio::level::low}
        );

        switch (cause) {
        case idfxx::sleep::wakeup_source::timer:
            logger.info("woke on timer");
            break;
        case idfxx::sleep::wakeup_source::gpio:
            logger.info("woke on BOOT button");
            // Wait for release; a held button would otherwise wake the next
            // sleep immediately too.
            while (pin.get_level() == idfxx::gpio::level::low) {
                idfxx::delay(10ms);
            }
            break;
        default:
            logger.warn("woke on unexpected source {}", cause);
            break;
        }
    }
}
