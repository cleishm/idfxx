// SPDX-License-Identifier: Apache-2.0

#include <idfxx/chrono>
#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <utility>

using namespace std::chrono_literals;
using idfxx::gpio;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // --- Pin info ---
    logger.info("=== Pin Info ===");
    auto led = idfxx::gpio_2;
    logger.info("LED pin: {}", led);
    logger.info("Pin number: {}", led.num());
    logger.info("Is connected: {}", led.is_connected());

    auto nc = idfxx::gpio_nc;
    logger.info("NC pin: {}", nc);
    logger.info("NC is connected: {}", nc.is_connected());

    // --- Output direction and level set/get ---
    logger.info("=== Output Basics ===");
    led.set_direction(gpio::mode::input_output);
    led.set_level(gpio::level::high);
    logger.info("Level after set high: {}", led.get_level());

    led.set_level(gpio::level::low);
    logger.info("Level after set low: {}", led.get_level());

    // --- Level inversion with operator~ ---
    logger.info("=== Level Inversion ===");
    auto lvl = gpio::level::low;
    logger.info("Original: {}, Inverted: {}", lvl, ~lvl);

    // --- Toggle ---
    logger.info("=== Toggle ===");
    led.set_level(gpio::level::low);
    for (int i = 0; i < 4; ++i) {
        led.toggle_level();
        logger.info("After toggle {}: {}", i + 1, led.get_level());
        idfxx::delay(200ms);
    }

    // --- Drive capability ---
    logger.info("=== Drive Capability ===");
    led.set_drive_capability(gpio::drive_cap::cap_3);
    auto cap = led.get_drive_capability();
    logger.info("Drive capability: {}", std::to_underlying(cap));

    led.set_drive_capability(gpio::drive_cap::cap_default);
    cap = led.get_drive_capability();
    logger.info("Default drive capability: {}", std::to_underlying(cap));

    // --- Batch configuration ---
    logger.info("=== Batch Configuration ===");
    gpio::config cfg{
        .mode = gpio::mode::output,
        .pull_mode = gpio::pull_mode::floating,
    };
    idfxx::configure_gpios(cfg, led);
    logger.info("Batch configured LED as output");

    // --- Drift-free blink loop ---
    logger.info("=== Drift-Free Blink (5 seconds) ===");
    led.set_level(gpio::level::low);
    auto next = idfxx::chrono::tick_clock::now() + 500ms;
    for (int i = 0; i < 10; ++i) {
        led.toggle_level();
        logger.info("Blink {}: {}", i + 1, led.get_level());
        idfxx::delay_until(next);
        next += 500ms;
    }

    // --- Cleanup ---
    logger.info("=== Cleanup ===");
    led.reset();
    logger.info("Pin reset. Done!");
}
