// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx sleep
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/sleep>
#include <unity.h>

#include <chrono>
#include <esp_sleep.h>
#include <string>
#include <utility>

using namespace idfxx;
using namespace std::chrono_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// wakeup_source values match ESP-IDF constants for the version-stable prefix
// (IDF v6.0 inserted UART1/UART2 after uart, shifting every later value; the
// implementation translates those sources instead of relying on equal values).
static_assert(std::to_underlying(sleep::wakeup_source::ext0) == ESP_SLEEP_WAKEUP_EXT0);
static_assert(std::to_underlying(sleep::wakeup_source::ext1) == ESP_SLEEP_WAKEUP_EXT1);
static_assert(std::to_underlying(sleep::wakeup_source::timer) == ESP_SLEEP_WAKEUP_TIMER);
static_assert(std::to_underlying(sleep::wakeup_source::touchpad) == ESP_SLEEP_WAKEUP_TOUCHPAD);
static_assert(std::to_underlying(sleep::wakeup_source::ulp) == ESP_SLEEP_WAKEUP_ULP);
static_assert(std::to_underlying(sleep::wakeup_source::gpio) == ESP_SLEEP_WAKEUP_GPIO);
static_assert(std::to_underlying(sleep::wakeup_source::uart) == ESP_SLEEP_WAKEUP_UART);

#if SOC_PM_SUPPORT_EXT1_WAKEUP
// ext1_mode's low trigger is named all_low on the original ESP32 (the only
// chip where it wakes on all selected pins) and any_low everywhere else; only
// the matching enumerator is defined per target.
#if CONFIG_IDF_TARGET_ESP32
static_assert(std::to_underlying(sleep::ext1_mode::all_low) == ESP_EXT1_WAKEUP_ALL_LOW);
#else
static_assert(std::to_underlying(sleep::ext1_mode::any_low) == ESP_EXT1_WAKEUP_ANY_LOW);
#endif
static_assert(std::to_underlying(sleep::ext1_mode::any_high) == ESP_EXT1_WAKEUP_ANY_HIGH);
#endif

#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
// deep_sleep_gpio_mode values match ESP-IDF constants
static_assert(std::to_underlying(sleep::deep_sleep_gpio_mode::wake_low) == ESP_GPIO_WAKEUP_GPIO_LOW);
static_assert(std::to_underlying(sleep::deep_sleep_gpio_mode::wake_high) == ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif

// wake_spec is satisfied by exactly the wake-up source specification types
static_assert(sleep::wake_spec<sleep::timer_wake>);
static_assert(sleep::wake_spec<sleep::gpio_wake>);
#if SOC_PM_SUPPORT_EXT0_WAKEUP
static_assert(sleep::wake_spec<sleep::ext0_wake>);
#endif
#if SOC_PM_SUPPORT_EXT1_WAKEUP
static_assert(sleep::wake_spec<sleep::ext1_wake>);
#endif
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
static_assert(sleep::wake_spec<sleep::deep_sleep_gpio_wake>);
#endif
static_assert(!sleep::wake_spec<int>);
static_assert(!sleep::wake_spec<sleep::wakeup_source>);

// =============================================================================
// Runtime tests
// =============================================================================

TEST_CASE("first boot reports no wakeup cause", "[sleep]") {
    TEST_ASSERT_FALSE(sleep::wakeup_cause().has_value());
}

TEST_CASE("timer wakeup can be armed and disarmed", "[sleep]") {
    auto enabled = sleep::try_enable_wakeup(sleep::timer_wake{10ms});
    TEST_ASSERT_TRUE(enabled.has_value());

    auto disabled = sleep::try_disable_wakeup_source(sleep::wakeup_source::timer);
    TEST_ASSERT_TRUE(disabled.has_value());
}

TEST_CASE("negative timer wakeup duration is rejected", "[sleep]") {
    auto enabled = sleep::try_enable_wakeup(sleep::timer_wake{-1ms});
    TEST_ASSERT_FALSE(enabled.has_value());
    TEST_ASSERT_TRUE(enabled.error() == errc::invalid_arg);
}

TEST_CASE("light sleep rejects an invalid wake source without sleeping", "[sleep]") {
    auto cause = sleep::try_light_sleep(sleep::gpio_wake{gpio::nc(), gpio::level::low});
    TEST_ASSERT_FALSE(cause.has_value());
    TEST_ASSERT_TRUE(cause.error() == errc::invalid_arg);
}

#if SOC_PM_SUPPORT_EXT1_WAKEUP
TEST_CASE("ext1 wakeup rejects unconnected pins", "[sleep]") {
    auto enabled = sleep::try_enable_wakeup(sleep::ext1_wake{{gpio::nc()}, sleep::ext1_mode::any_high});
    TEST_ASSERT_FALSE(enabled.has_value());
    TEST_ASSERT_TRUE(enabled.error() == errc::invalid_arg);
}
#endif

// Light sleep needs real power-management hardware; QEMU does not emulate it
// (esp_light_sleep_start never returns there).
TEST_CASE("light sleep wakes on timer", "[sleep][hw]") {
    auto cause = sleep::try_light_sleep(sleep::timer_wake{50ms});
    TEST_ASSERT_TRUE(cause.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(sleep::wakeup_source::timer), std::to_underlying(*cause));

    // The wake source was disarmed on return.
    auto disabled = sleep::try_disable_wakeup_source(sleep::wakeup_source::timer);
    TEST_ASSERT_FALSE(disabled.has_value());
}

TEST_CASE("light sleep for a duration wakes on timer", "[sleep][hw]") {
    auto cause = sleep::try_light_sleep_for(50ms);
    TEST_ASSERT_TRUE(cause.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(sleep::wakeup_source::timer), std::to_underlying(*cause));
}

// =============================================================================
// Formatter tests
// =============================================================================

TEST_CASE("to_string names known wakeup sources", "[sleep]") {
    TEST_ASSERT_EQUAL_STRING("timer", sleep::to_string(sleep::wakeup_source::timer).c_str());
    TEST_ASSERT_EQUAL_STRING("gpio", sleep::to_string(sleep::wakeup_source::gpio).c_str());
    TEST_ASSERT_EQUAL_STRING("ext1", sleep::to_string(sleep::wakeup_source::ext1).c_str());
    TEST_ASSERT_EQUAL_STRING("other", sleep::to_string(sleep::wakeup_source::other).c_str());
}

TEST_CASE("to_string reports unrecognized wakeup sources", "[sleep]") {
    TEST_ASSERT_EQUAL_STRING("unknown(99)", sleep::to_string(static_cast<sleep::wakeup_source>(99)).c_str());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<sleep::wakeup_source, char>);

TEST_CASE("wakeup_source formatter outputs the source name", "[sleep]") {
    auto s = std::format("{}", sleep::wakeup_source::timer);
    TEST_ASSERT_EQUAL_STRING("timer", s.c_str());
    s = std::format("woke on {}", sleep::wakeup_source::gpio);
    TEST_ASSERT_EQUAL_STRING("woke on gpio", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
