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
    logger.info("PWM output started on GPIO 2");

    // Alternatively, select a specific timer and channel:
    //   auto tmr = idfxx::pwm::timer_0;
    //   tmr.configure({.frequency = 5_kHz});
    //   auto led = idfxx::pwm::start(idfxx::gpio_2, tmr, idfxx::pwm::channel::ch_0);

    // Cycle through brightness levels
    const float levels[] = {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (auto duty : levels) {
            led.set_duty(duty);
            logger.info("Duty: {:.0f}% (ticks: {})", duty * 100, led.duty_ticks());
            idfxx::delay(500ms);
        }
    }

    // Output stops automatically when `led` goes out of scope
    logger.info("Done!");
}
