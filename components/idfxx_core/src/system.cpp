// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/system>

#include <esp_system.h>
#include <utility>

// Verify reset_reason values match ESP-IDF constants
static_assert(std::to_underlying(idfxx::reset_reason::unknown) == ESP_RST_UNKNOWN);
static_assert(std::to_underlying(idfxx::reset_reason::power_on) == ESP_RST_POWERON);
static_assert(std::to_underlying(idfxx::reset_reason::external) == ESP_RST_EXT);
static_assert(std::to_underlying(idfxx::reset_reason::software) == ESP_RST_SW);
static_assert(std::to_underlying(idfxx::reset_reason::panic) == ESP_RST_PANIC);
static_assert(std::to_underlying(idfxx::reset_reason::interrupt_watchdog) == ESP_RST_INT_WDT);
static_assert(std::to_underlying(idfxx::reset_reason::task_watchdog) == ESP_RST_TASK_WDT);
static_assert(std::to_underlying(idfxx::reset_reason::watchdog) == ESP_RST_WDT);
static_assert(std::to_underlying(idfxx::reset_reason::deep_sleep) == ESP_RST_DEEPSLEEP);
static_assert(std::to_underlying(idfxx::reset_reason::brownout) == ESP_RST_BROWNOUT);
static_assert(std::to_underlying(idfxx::reset_reason::sdio) == ESP_RST_SDIO);
static_assert(std::to_underlying(idfxx::reset_reason::usb) == ESP_RST_USB);
static_assert(std::to_underlying(idfxx::reset_reason::jtag) == ESP_RST_JTAG);
static_assert(std::to_underlying(idfxx::reset_reason::efuse) == ESP_RST_EFUSE);
static_assert(std::to_underlying(idfxx::reset_reason::power_glitch) == ESP_RST_PWR_GLITCH);
static_assert(std::to_underlying(idfxx::reset_reason::cpu_lockup) == ESP_RST_CPU_LOCKUP);

namespace idfxx {

std::string to_string(reset_reason r) {
    switch (r) {
    case reset_reason::unknown:
        return "UNKNOWN";
    case reset_reason::power_on:
        return "POWER_ON";
    case reset_reason::external:
        return "EXTERNAL";
    case reset_reason::software:
        return "SOFTWARE";
    case reset_reason::panic:
        return "PANIC";
    case reset_reason::interrupt_watchdog:
        return "INTERRUPT_WATCHDOG";
    case reset_reason::task_watchdog:
        return "TASK_WATCHDOG";
    case reset_reason::watchdog:
        return "WATCHDOG";
    case reset_reason::deep_sleep:
        return "DEEP_SLEEP";
    case reset_reason::brownout:
        return "BROWNOUT";
    case reset_reason::sdio:
        return "SDIO";
    case reset_reason::usb:
        return "USB";
    case reset_reason::jtag:
        return "JTAG";
    case reset_reason::efuse:
        return "EFUSE";
    case reset_reason::power_glitch:
        return "POWER_GLITCH";
    case reset_reason::cpu_lockup:
        return "CPU_LOCKUP";
    default:
        return "unknown(" + std::to_string(static_cast<int>(r)) + ")";
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void register_shutdown_handler(void (*handler)()) {
    unwrap(try_register_shutdown_handler(handler));
}

void unregister_shutdown_handler(void (*handler)()) {
    unwrap(try_unregister_shutdown_handler(handler));
}
#endif

result<void> try_register_shutdown_handler(void (*handler)()) {
    return wrap(esp_register_shutdown_handler(handler));
}

result<void> try_unregister_shutdown_handler(void (*handler)()) {
    return wrap(esp_unregister_shutdown_handler(handler));
}

} // namespace idfxx
