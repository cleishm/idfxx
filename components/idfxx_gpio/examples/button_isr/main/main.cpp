// SPDX-License-Identifier: Apache-2.0

#include <idfxx/chrono>
#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sched>

#include <atomic>
#include <chrono>

using namespace std::chrono_literals;
using idfxx::gpio;

static constexpr idfxx::log::logger logger{"example"};

// BOOT button (active-low). It is on GPIO 9 on the ESP32-C3, ESP32-C6 and
// ESP32-H2 devkits, and on GPIO 0 on the others.
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
static constexpr auto button_pin = idfxx::gpio_9;
#else
static constexpr auto button_pin = idfxx::gpio_0;
#endif

static std::atomic<int> press_count{0};

extern "C" void app_main() {
    // --- Configure button pin ---
    logger.info("=== Button ISR Example ===");
    idfxx::gpio button = button_pin;
    logger.info("Button pin: {}", button);

    button.set_direction(gpio::mode::input);
    button.set_pull_mode(gpio::pull_mode::pullup);
    logger.info("Configured as input with pullup");

    // --- Install ISR service ---
    gpio::install_isr_service();
    logger.info("ISR service installed");

    // --- Set interrupt type ---
    button.set_intr_type(gpio::intr_type::negedge);
    logger.info("Interrupt type set to negedge (triggers on button press)");

    // --- Register ISR handler ---
    // Lambda captures nothing — uses file-scope atomic for ISR safety
    gpio::unique_isr_handle isr_handle{button.isr_handler_add([]() {
        press_count.fetch_add(1, std::memory_order_relaxed);
    })};
    logger.info("ISR handler registered (RAII handle)");

    // --- Enable interrupts ---
    button.intr_enable();
    logger.info("Interrupts enabled");

    // --- Monitor for 30 seconds ---
    logger.info("Monitoring button presses for 30 seconds...");
    logger.info("Press the BOOT button (GPIO {}) to trigger interrupts.", button.num());
    int last_count = 0;
    auto end_time = idfxx::chrono::tick_clock::now() + 30s;

    while (idfxx::chrono::tick_clock::now() < end_time) {
        int current = press_count.load(std::memory_order_relaxed);
        if (current != last_count) {
            logger.info("Button pressed! Count: {}", current);
            last_count = current;
        }
        idfxx::delay(50ms);
    }

    logger.info("Monitoring complete. Total presses: {}", press_count.load(std::memory_order_relaxed));

    // --- Cleanup (ordered) ---
    logger.info("=== Cleanup ===");
    button.intr_disable();
    logger.info("Interrupts disabled");

    isr_handle = gpio::unique_isr_handle{}; // RAII removal of ISR handler
    logger.info("ISR handler removed");

    gpio::uninstall_isr_service();
    logger.info("ISR service uninstalled");

    button.reset();
    logger.info("Pin reset. Done!");
}
