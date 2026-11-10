// SPDX-License-Identifier: Apache-2.0

// Wakes the ESP32 from deep sleep with a PCF8563 alarm, once a minute. Each boot reports why it woke,
// sets the alarm for the next minute and goes back to sleep; the chip's INT pin does the waking.

#include <idfxx/i2c/master>
#include <idfxx/log>
#include <idfxx/rtc/pcf8563>
#include <idfxx/sleep>

#include <chrono>
#include <soc/soc_caps.h>

#if !SOC_PM_SUPPORT_EXT0_WAKEUP
#error "alarm_wake needs EXT0 wake-up (ESP32, ESP32-S2, ESP32-S3)"
#endif

using namespace std::chrono;
using namespace quanta::frequency_literals;
using idfxx::rtc::pcf8563;

static constexpr idfxx::log::logger logger{"rtc"};

// Pin assignments — change to match your board.
static constexpr auto PIN_SDA = idfxx::gpio_8;
static constexpr auto PIN_SCL = idfxx::gpio_9;
// The chip's INT output is open-drain and active-low, so it needs a pull-up. GPIO 4 is RTC-capable,
// which EXT0 wake-up requires.
static constexpr auto PIN_INT = idfxx::gpio_4;

// The time to seed the clock with when it has lost track: 2026-01-01T00:00:00 UTC.
static constexpr sys_seconds SEED_TIME{sys_days{2026y / January / 1}};

// How long to sleep between alarms.
static constexpr auto ALARM_INTERVAL = 1min;

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

        auto now = rtc.now();
        if (!now) {
            // The chip lost power or its oscillator stopped, so its time can't be trusted.
            logger.warn("RTC time was lost; seeding it with {:%F %T} UTC", SEED_TIME);
            rtc.set_time(SEED_TIME);
            now = SEED_TIME;
        }

        // An EXT0 wake means INT woke the chip, and the alarm flag says the alarm drove it. The flag
        // alone isn't enough: an alarm that fires while the chip is awake, or before a reset, also sets it.
        auto events = rtc.events();
        if (idfxx::sleep::wakeup_cause() == idfxx::sleep::wakeup_source::ext0 &&
            events.contains(pcf8563::event::alarm)) {
            logger.info("woke on the RTC alarm at {:%F %T} UTC", *now);
        } else {
            logger.info("first boot or another reset, at {:%F %T} UTC", *now);
        }
        // Clearing the flag releases INT. EXT0 is level-triggered, so a pin still held low would wake
        // the chip again at once.
        rtc.clear_events(events);

        // The alarm has whole-minute resolution, so aim for a minute boundary, and skip to the next one
        // if this is under 30 s away: the chip could pass it before the ESP32 is asleep.
        auto next = floor<minutes>(*now) + ALARM_INTERVAL;
        if (next - *now < 30s) {
            next += 1min;
        }

        // The alarm matches the minute and hour, so it fires once a day at that time; setting it again
        // on every boot is what makes it repeat.
        hh_mm_ss next_time{next - floor<days>(next)};
        rtc.set_alarm({.minute = next_time.minutes(), .hour = next_time.hours(), .interrupt = true});
        logger.info("sleeping until {:%F %T} UTC", next);

        idfxx::sleep::deep_sleep(idfxx::sleep::ext0_wake{PIN_INT, idfxx::gpio::level::low});
    } catch (const std::system_error& e) {
        logger.error("failed: {}", e.what());
    }
}
