// SPDX-License-Identifier: Apache-2.0

// Deep sleep with button and timer wake-up.
//
// Each boot reports why the chip woke, then deep-sleeps for up to 30 seconds
// with the BOOT button (GPIO 0) armed as a wake-up pin. Waking from deep
// sleep restarts the application from the beginning — only RTC memory
// survives, demonstrated here with a boot counter.

#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sleep>

#include <chrono>
#include <esp_attr.h>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"deepsleep"};

// BOOT button: pulled low while pressed. RTC-capable on chips with EXT1
// wake-up, deep-sleep wake-capable on the others.
static constexpr auto WAKE_PIN = idfxx::gpio_0;

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
            logger.info("woke from deep sleep on BOOT button (ext1 status {:#x})", idfxx::sleep::ext1_wakeup_status());
            break;
#endif
        case idfxx::sleep::wakeup_source::gpio:
            logger.info("woke from deep sleep on BOOT button");
            break;
        default:
            logger.warn("woke from deep sleep on unexpected source {}", *cause);
            break;
        }
    } else {
        logger.info("cold boot (not a wake from sleep)");
    }

    logger.info("entering deep sleep ({} timer, or press BOOT)", SLEEP_INTERVAL);

    // The BOOT strap has an external pull-up on most boards, so the pin idles
    // high and a press pulls it low.
#if SOC_PM_SUPPORT_EXT1_WAKEUP
    // The original ESP32 only offers an all-pins-low trigger; every other EXT1
    // chip offers any-pin-low. With a single wake pin the two are equivalent.
#if CONFIG_IDF_TARGET_ESP32
    constexpr auto ext1_low = idfxx::sleep::ext1_mode::all_low;
#else
    constexpr auto ext1_low = idfxx::sleep::ext1_mode::any_low;
#endif
    idfxx::sleep::deep_sleep(idfxx::sleep::ext1_wake{{WAKE_PIN}, ext1_low}, idfxx::sleep::timer_wake{SLEEP_INTERVAL});
#elif SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
    idfxx::sleep::deep_sleep(
        idfxx::sleep::deep_sleep_gpio_wake{{WAKE_PIN}, idfxx::sleep::deep_sleep_gpio_mode::wake_low},
        idfxx::sleep::timer_wake{SLEEP_INTERVAL}
    );
#else
    idfxx::sleep::deep_sleep(SLEEP_INTERVAL);
#endif
}
