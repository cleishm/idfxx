// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx system types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/system.hpp"
#include "unity.h"

#include <esp_system.h>
#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::is_enum_v<reset_reason>);
static_assert(std::is_same_v<std::underlying_type_t<reset_reason>, int>);

// reset_reason values match ESP-IDF constants
static_assert(static_cast<int>(reset_reason::unknown) == ESP_RST_UNKNOWN);
static_assert(static_cast<int>(reset_reason::power_on) == ESP_RST_POWERON);
static_assert(static_cast<int>(reset_reason::external) == ESP_RST_EXT);
static_assert(static_cast<int>(reset_reason::software) == ESP_RST_SW);
static_assert(static_cast<int>(reset_reason::panic) == ESP_RST_PANIC);
static_assert(static_cast<int>(reset_reason::interrupt_watchdog) == ESP_RST_INT_WDT);
static_assert(static_cast<int>(reset_reason::task_watchdog) == ESP_RST_TASK_WDT);
static_assert(static_cast<int>(reset_reason::watchdog) == ESP_RST_WDT);
static_assert(static_cast<int>(reset_reason::deep_sleep) == ESP_RST_DEEPSLEEP);
static_assert(static_cast<int>(reset_reason::brownout) == ESP_RST_BROWNOUT);
static_assert(static_cast<int>(reset_reason::sdio) == ESP_RST_SDIO);
static_assert(static_cast<int>(reset_reason::usb) == ESP_RST_USB);
static_assert(static_cast<int>(reset_reason::jtag) == ESP_RST_JTAG);
static_assert(static_cast<int>(reset_reason::efuse) == ESP_RST_EFUSE);
static_assert(static_cast<int>(reset_reason::power_glitch) == ESP_RST_PWR_GLITCH);
static_assert(static_cast<int>(reset_reason::cpu_lockup) == ESP_RST_CPU_LOCKUP);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// last_reset_reason

TEST_CASE("last_reset_reason returns a valid value", "[idfxx][system]") {
    auto reason = last_reset_reason();
    TEST_ASSERT_TRUE(static_cast<int>(reason) >= 0);
    TEST_ASSERT_TRUE(static_cast<int>(reason) <= 15);
}

// to_string

TEST_CASE("to_string(reset_reason) returns non-empty for all values", "[idfxx][system]") {
    for (int i = 0; i <= 15; ++i) {
        auto s = to_string(static_cast<reset_reason>(i));
        TEST_ASSERT_FALSE(s.empty());
    }
}

TEST_CASE("to_string(reset_reason::power_on) returns POWER_ON", "[idfxx][system]") {
    TEST_ASSERT_EQUAL_STRING("POWER_ON", to_string(reset_reason::power_on).c_str());
}

TEST_CASE("to_string(reset_reason) handles unknown values", "[idfxx][system]") {
    auto unknown = static_cast<reset_reason>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", to_string(unknown).c_str());
}

// Heap info

TEST_CASE("free_heap_size returns non-zero", "[idfxx][system]") {
    TEST_ASSERT_GREATER_THAN(0, free_heap_size());
}

TEST_CASE("free_internal_heap_size returns non-zero", "[idfxx][system]") {
    TEST_ASSERT_GREATER_THAN(0, free_internal_heap_size());
}

TEST_CASE("minimum_free_heap_size returns non-zero", "[idfxx][system]") {
    TEST_ASSERT_GREATER_THAN(0, minimum_free_heap_size());
}

TEST_CASE("minimum_free_heap_size <= free_heap_size", "[idfxx][system]") {
    TEST_ASSERT_LESS_OR_EQUAL(free_heap_size(), minimum_free_heap_size());
}

// Shutdown handlers

static void test_shutdown_handler() {}

TEST_CASE("register and unregister shutdown handler", "[idfxx][system]") {
    auto r1 = try_register_shutdown_handler(test_shutdown_handler);
    TEST_ASSERT_TRUE(r1.has_value());

    auto r2 = try_unregister_shutdown_handler(test_shutdown_handler);
    TEST_ASSERT_TRUE(r2.has_value());
}

TEST_CASE("unregister non-registered shutdown handler fails", "[idfxx][system]") {
    auto r = try_unregister_shutdown_handler(test_shutdown_handler);
    TEST_ASSERT_FALSE(r.has_value());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
// Formatter
static_assert(std::formattable<reset_reason, char>);

TEST_CASE("reset_reason formatter outputs POWER_ON", "[idfxx][system]") {
    TEST_ASSERT_EQUAL_STRING("POWER_ON", std::format("{}", reset_reason::power_on).c_str());
}

TEST_CASE("reset_reason formatter handles unknown values", "[idfxx][system]") {
    auto unknown = static_cast<reset_reason>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", std::format("{}", unknown).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
