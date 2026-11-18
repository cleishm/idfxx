// SPDX-License-Identifier: Apache-2.0

// Restores the system time from a PCF8563 real-time clock at boot, seeding the
// clock with a fixed time if it has lost track.

#include <idfxx/chrono>
#include <idfxx/i2c/master>
#include <idfxx/log>
#include <idfxx/rtc/pcf8563>

#include <chrono>

using namespace std::chrono;
using namespace quanta::frequency_literals;

static constexpr idfxx::log::logger logger{"rtc"};

// Pin assignments — change to match your board.
static constexpr auto PIN_SDA = idfxx::gpio_8;
static constexpr auto PIN_SCL = idfxx::gpio_9;

// The time to seed the clock with when it has lost track: 2026-01-01T00:00:00 UTC.
static constexpr sys_seconds SEED_TIME{sys_days{2026y / January / 1}};

extern "C" void app_main() {
    try {
        idfxx::i2c::master_bus bus(
            idfxx::i2c::port::i2c0,
            {
                .sda = PIN_SDA,
                .scl = PIN_SCL,
                .frequency = 400_kHz,
            }
        );

        idfxx::rtc::pcf8563 rtc(bus);

        if (auto now = rtc.now()) {
            idfxx::chrono::set_system_time(*now);
            logger.info("System time set from the RTC: {:%F %T} UTC", *now);
        } else {
            // The chip lost power or its oscillator stopped, so its time can't be trusted.
            logger.warn("RTC time was lost; seeding it with {:%F %T} UTC", SEED_TIME);
            rtc.set_time(SEED_TIME);
        }
    } catch (const std::system_error& e) {
        logger.error("failed: {}", e.what());
    }
}
