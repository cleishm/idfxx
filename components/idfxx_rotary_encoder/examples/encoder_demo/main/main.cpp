// SPDX-License-Identifier: Apache-2.0

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/rotary_encoder>
#include <idfxx/sched>

#include <atomic>
#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// GPIO pins — customize these for your hardware
static constexpr auto pin_a = idfxx::gpio_4;
static constexpr auto pin_b = idfxx::gpio_5;

extern "C" void app_main() {
    logger.info("=== Rotary Encoder Demo ===");
    logger.info("Pin A: {}, Pin B: {}", pin_a, pin_b);

    std::atomic<int32_t> position{0};

    idfxx::rotary_encoder encoder({
        .pin_a = pin_a,
        .pin_b = pin_b,
        .callback = [&position](int32_t diff) {
            position += diff;
            idfxx::log::info("encoder", "Position changed by {}, now {}", diff, position.load());
        },
    });

    // --- Monitor without acceleration ---
    logger.info("=== No Acceleration (15 seconds) ===");
    for (int i = 0; i < 15; ++i) {
        idfxx::delay(1s);
        logger.info("Position: {}", position.load());
    }

    // --- Enable acceleration ---
    logger.info("=== Acceleration Enabled, coefficient=10 (15 seconds) ===");
    encoder.enable_acceleration(10);
    for (int i = 0; i < 15; ++i) {
        idfxx::delay(1s);
        logger.info("Position: {}", position.load());
    }

    // --- Disable acceleration ---
    logger.info("=== Acceleration Disabled (10 seconds) ===");
    encoder.disable_acceleration();
    for (int i = 0; i < 10; ++i) {
        idfxx::delay(1s);
        logger.info("Position: {}", position.load());
    }

    logger.info("Final position: {}. Done!", position.load());
}
