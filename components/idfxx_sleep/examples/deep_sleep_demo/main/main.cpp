// SPDX-License-Identifier: Apache-2.0

// Deep sleep with button and timer wake-up.
//
// Each boot reports why the chip woke, then deep-sleeps for up to 30 seconds
// with a button armed as a wake-up pin. Waking from deep sleep restarts the
// application from the beginning — only RTC memory survives, demonstrated here
// with a boot counter.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sleep>

#include <chrono>
#include <esp_attr.h>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"deepsleep"};

// Button pulled low while pressed. On the ESP32-H2, the BOOT button's GPIO 9
// is among the pins that can wake it from deep sleep (GPIO 7-14), but GPIO 0
// isn't. Elsewhere, GPIO 0 can wake the chip: it is the BOOT button on the
// ESP32, ESP32-S2 and ESP32-S3, while the ESP32-C3 and ESP32-C6 need a button
// wired to it because their BOOT button (GPIO 9) can't wake them.
#if CONFIG_IDF_TARGET_ESP32H2
static constexpr auto WAKE_PIN = idfxx::gpio_9;
#else
static constexpr auto WAKE_PIN = idfxx::gpio_0;
#endif

// Maximum time to stay in deep sleep before the timer wakes the chip.
static constexpr auto SLEEP_INTERVAL = 30s;

// Survives deep sleep in RTC memory; lost on any other reset.
RTC_DATA_ATTR static int boot_count = 0;

extern "C" void app_main() {
    ++boot_count;
    logger.info("=== Deep Sleep Demo (boot {}) ===", boot_count);

    if (auto cause = idfxx::sleep::wakeup_cause()) {
        switch (*cause) {
        case idfxx::sleep::wakeup_source::timer:
            logger.info("woke from deep sleep on timer");
            break;
#if SOC_PM_SUPPORT_EXT1_WAKEUP
        case idfxx::sleep::wakeup_source::ext1:
            logger.info("woke from deep sleep on button (ext1 status {:#x})", idfxx::sleep::ext1_wakeup_status());
            break;
#endif
        case idfxx::sleep::wakeup_source::gpio:
            logger.info("woke from deep sleep on button");
            break;
        default:
            logger.warn("woke from deep sleep on unexpected source {}", *cause);
            break;
        }
    } else {
        logger.info("cold boot (not a wake from sleep)");
    }

    logger.info("entering deep sleep ({} timer, or press the button)", SLEEP_INTERVAL);

#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP && !SOC_PM_SUPPORT_EXT0_WAKEUP
    // On the ESP32-C3 and ESP32-C6, GPIO wake-up keeps the pin's internal
    // pull-up enabled during deep sleep, so the pin idles high and a press
    // pulls it low.
    idfxx::sleep::deep_sleep(
        idfxx::sleep::deep_sleep_gpio_wake{{WAKE_PIN}, idfxx::sleep::deep_sleep_gpio_mode::wake_low},
        idfxx::sleep::timer_wake{SLEEP_INTERVAL}
    );
#elif SOC_PM_SUPPORT_EXT1_WAKEUP
    // The BOOT button has an external pull-up, so the pin idles high and a
    // press pulls it low. The original ESP32 only offers an all-pins-low
    // trigger; every other EXT1 chip offers any-pin-low. With a single wake pin
    // the two are equivalent.
#if CONFIG_IDF_TARGET_ESP32
    constexpr auto ext1_low = idfxx::sleep::ext1_mode::all_low;
#else
    constexpr auto ext1_low = idfxx::sleep::ext1_mode::any_low;
#endif
    idfxx::sleep::deep_sleep(idfxx::sleep::ext1_wake{{WAKE_PIN}, ext1_low}, idfxx::sleep::timer_wake{SLEEP_INTERVAL});
#else
    idfxx::sleep::deep_sleep(SLEEP_INTERVAL);
#endif
}
