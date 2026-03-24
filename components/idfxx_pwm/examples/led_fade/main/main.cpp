// SPDX-License-Identifier: Apache-2.0

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/pwm>
#include <idfxx/sched>

#include <chrono>

using namespace frequency_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    // Start PWM output on GPIO 2 (built-in LED on many boards).
    // A free timer and channel are allocated automatically.
    auto led = idfxx::pwm::start(idfxx::gpio_2, {.frequency = 5_kHz});

    // Install the fade service (required before using any fade operations)
    idfxx::pwm::output::install_fade_service();
    logger.info("Fade service installed");

    // Breathing effect: fade up and down repeatedly
    for (int i = 0; i < 5; ++i) {
        logger.info("Breath cycle {}", i + 1);

        // Fade from off to full brightness over 1 second
        led.fade_to(1.0f, 1s, idfxx::pwm::fade_mode::wait_done);

        // Fade from full brightness to off over 1 second
        led.fade_to(0.0f, 1s, idfxx::pwm::fade_mode::wait_done);
    }

    // Non-blocking fade: returns immediately
    logger.info("Starting non-blocking fade...");
    led.fade_to(0.5f, 2s);
    logger.info("Fade running in background, doing other work...");
    idfxx::delay(2s);

    logger.info("Final duty: {:.0f}%", led.duty() * 100);

    // Cleanup: uninstall fade service and let the output stop on destruction
    idfxx::pwm::output::uninstall_fade_service();
    logger.info("Done!");
}
