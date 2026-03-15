// SPDX-License-Identifier: Apache-2.0

#include <idfxx/button>
#include <idfxx/chrono>
#include <idfxx/log>
#include <idfxx/sched>

#include <atomic>
#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

static std::atomic<int> click_count{0};
static std::atomic<int> long_press_count{0};

extern "C" void app_main() {
    // --- Create button ---
    logger.info("=== Button Demo ===");
    logger.info("Creating button on GPIO 0 (BOOT button)");

    idfxx::button btn({
        .pin = idfxx::gpio_0,
        .pressed_level = idfxx::gpio::level::low,
        .enable_pull = true,
        .callback =
            [](idfxx::button::event_type event) {
                switch (event) {
                case idfxx::button::event_type::pressed:
                    break;
                case idfxx::button::event_type::released:
                    break;
                case idfxx::button::event_type::clicked:
                    click_count.fetch_add(1, std::memory_order_relaxed);
                    break;
                case idfxx::button::event_type::long_press:
                    long_press_count.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
            },
    });

    logger.info("Button created with config:");
    logger.info("  pressed_level: low (active-low BOOT button)");
    logger.info("  enable_pull: true (internal pullup)");
    logger.info("  dead_time: 50ms (debounce)");
    logger.info("  long_press_time: 1000ms");

    // --- Monitor button events ---
    logger.info("Monitoring button events for 30 seconds...");
    logger.info("Short press -> clicked event, hold >1s -> long_press event");

    auto end_time = idfxx::chrono::tick_clock::now() + 30s;
    int last_clicks = 0;
    int last_long = 0;

    while (idfxx::chrono::tick_clock::now() < end_time) {
        int clicks = click_count.load(std::memory_order_relaxed);
        int longs = long_press_count.load(std::memory_order_relaxed);

        if (clicks != last_clicks) {
            logger.info("Click! Total clicks: {}", clicks);
            last_clicks = clicks;
        }
        if (longs != last_long) {
            logger.info("Long press! Total long presses: {}", longs);
            last_long = longs;
        }

        idfxx::delay(50ms);
    }

    // --- Summary ---
    logger.info("=== Summary ===");
    logger.info("Clicks: {}", click_count.load(std::memory_order_relaxed));
    logger.info("Long presses: {}", long_press_count.load(std::memory_order_relaxed));
    logger.info("Done!");
}
