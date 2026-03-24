// SPDX-License-Identifier: Apache-2.0

// Plays a simple melody using a passive buzzer connected to a GPIO pin.
// The buzzer tone is controlled by changing the PWM timer frequency.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/pwm>
#include <idfxx/sched>

#include <chrono>

using namespace frequency_literals;
using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Note frequencies (Hz)
constexpr auto NOTE_C4 = 262_Hz;
constexpr auto NOTE_D4 = 294_Hz;
constexpr auto NOTE_E4 = 330_Hz;
constexpr auto NOTE_F4 = 349_Hz;
constexpr auto NOTE_G4 = 392_Hz;
constexpr auto NOTE_A4 = 440_Hz;
constexpr auto NOTE_B4 = 494_Hz;
constexpr auto NOTE_C5 = 523_Hz;

struct note {
    freq::hertz frequency;
    std::chrono::milliseconds duration;
};

// Simple scale
constexpr note melody[] = {
    {NOTE_C4, 300ms},
    {NOTE_D4, 300ms},
    {NOTE_E4, 300ms},
    {NOTE_F4, 300ms},
    {NOTE_G4, 300ms},
    {NOTE_A4, 300ms},
    {NOTE_B4, 300ms},
    {NOTE_C5, 500ms},
};

extern "C" void app_main() {
    // Use an explicit timer since we need the handle to change frequency for each note.
    // Low duty resolution since tone is controlled by frequency, not duty.
    auto tmr = idfxx::pwm::timer_0;
    tmr.configure({.frequency = NOTE_C4, .resolution_bits = 8});

    // 50% duty gives the loudest tone on a passive buzzer
    auto buzzer = idfxx::pwm::start(idfxx::gpio_18, tmr, idfxx::pwm::channel::ch_0, {.duty = 0.5f});

    logger.info("Playing melody...");
    for (const auto& [freq, dur] : melody) {
        logger.info("  {} Hz for {} ms", freq, dur.count());
        tmr.set_frequency(freq);
        idfxx::delay(dur);
    }

    // Silence: set duty to 0
    buzzer.set_duty(0.0f);
    logger.info("Done!");
}
