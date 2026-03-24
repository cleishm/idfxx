// SPDX-License-Identifier: Apache-2.0

// Controls a standard hobby servo motor using PWM.
// Servos expect a 50 Hz signal (20 ms period) with a pulse width of 1-2 ms
// (corresponding to 0-180 degrees).

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/pwm>
#include <idfxx/sched>

#include <chrono>

using namespace frequency_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Standard servo pulse widths:
//   1000 us =   0 degrees
//   1500 us =  90 degrees
//   2000 us = 180 degrees
constexpr auto min_pulse = 1000us; // 0 degrees
constexpr auto max_pulse = 2000us; // 180 degrees

/// Converts an angle (0-180) to a pulse width for a standard servo.
constexpr std::chrono::microseconds angle_to_pulse(int angle) {
    return min_pulse + (max_pulse - min_pulse) * angle / 180;
}

extern "C" void app_main() {
    // 50 Hz with 14-bit resolution for precise pulse width control.
    // A free timer and channel are allocated automatically.
    auto servo = idfxx::pwm::start(idfxx::gpio_18, {.frequency = 50_Hz, .resolution_bits = 14});

    // Sweep from 0 to 180 degrees
    logger.info("Sweeping servo 0 -> 180 degrees...");
    for (int angle = 0; angle <= 180; angle += 10) {
        auto pulse = angle_to_pulse(angle);
        servo.set_pulse_width(pulse);
        logger.info("  {} degrees ({} μs)", angle, pulse.count());
        idfxx::delay(200ms);
    }

    // Sweep back
    logger.info("Sweeping servo 180 -> 0 degrees...");
    for (int angle = 180; angle >= 0; angle -= 10) {
        servo.set_pulse_width(angle_to_pulse(angle));
        idfxx::delay(200ms);
    }

    // Center the servo
    servo.set_pulse_width(angle_to_pulse(90));
    logger.info("Servo centered at 90 degrees. Done!");
}
