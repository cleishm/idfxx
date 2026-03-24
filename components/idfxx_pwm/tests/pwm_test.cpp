// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::pwm
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/pwm"
#include "unity.h"

#include <chrono>
#include <type_traits>
#include <utility>

using namespace idfxx::pwm;
using namespace frequency_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// timer is a value type: copyable and movable (not default constructible — needs a number)
static_assert(!std::is_default_constructible_v<timer>);
static_assert(std::is_copy_constructible_v<timer>);
static_assert(std::is_copy_assignable_v<timer>);
static_assert(std::is_move_constructible_v<timer>);
static_assert(std::is_move_assignable_v<timer>);

// timer::config is an aggregate with sensible defaults
static_assert(std::is_aggregate_v<timer::config>);
static_assert(std::is_default_constructible_v<timer::config>);

// Predefined timer constants have correct numbers
static_assert(timer_0.idf_num() == 0);
static_assert(timer_1.idf_num() == 1);
static_assert(timer_2.idf_num() == 2);
static_assert(timer_3.idf_num() == 3);

// Predefined timers default to low speed
static_assert(timer_0.speed_mode() == speed_mode::low_speed);
static_assert(timer_3.speed_mode() == speed_mode::low_speed);

#if SOC_LEDC_SUPPORT_HS_MODE
// High-speed timer constants
static_assert(hs_timer_0.idf_num() == 0);
static_assert(hs_timer_0.speed_mode() == speed_mode::high_speed);
static_assert(hs_timer_3.idf_num() == 3);
static_assert(hs_timer_3.speed_mode() == speed_mode::high_speed);
#endif


// output is non-copyable and move-only
static_assert(!std::is_default_constructible_v<output>);
static_assert(!std::is_copy_constructible_v<output>);
static_assert(!std::is_copy_assignable_v<output>);
static_assert(std::is_move_constructible_v<output>);
static_assert(std::is_move_assignable_v<output>);

// output_config is an aggregate with sensible defaults
static_assert(std::is_aggregate_v<output_config>);
static_assert(std::is_default_constructible_v<output_config>);


// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("timer try_configure with zero frequency returns error", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    auto result = tmr.try_configure({.frequency = 0_Hz});
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), result.error().value());
}

TEST_CASE("timer try_configure with valid config succeeds", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    auto result = tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(13, tmr.resolution_bits());
    TEST_ASSERT_EQUAL(1u << 13, tmr.ticks_max());
}

TEST_CASE("timer try_configure convenience overload succeeds", "[idfxx][pwm][timer]") {
    auto tmr = timer_1;
    auto result = tmr.try_configure(1000_Hz, 10);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(10, tmr.resolution_bits());
}

TEST_CASE("timer frequency reads back configured value", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    auto result = tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13});
    TEST_ASSERT_TRUE(result.has_value());

    auto freq = tmr.frequency();
    // Frequency may differ slightly due to clock divider rounding
    TEST_ASSERT_INT_WITHIN(100, 5000, static_cast<int>(freq.count()));
}

TEST_CASE("timer unconfigured frequency returns zero", "[idfxx][pwm][timer]") {
    auto tmr = timer_2;
    TEST_ASSERT_EQUAL(0, tmr.frequency().count());
}

TEST_CASE("timer period returns expected value", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto p = tmr.period();
    // 1/5000 Hz = 200000 ns = 200 us
    TEST_ASSERT_INT_WITHIN(5000, 200000, static_cast<int>(p.count()));
}

TEST_CASE("timer tick_period returns expected value", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto tp = tmr.tick_period();
    // period ~200000 ns / 8192 ticks ~= 24 ns
    TEST_ASSERT_INT_WITHIN(5, 24, static_cast<int>(tp.count()));
}

TEST_CASE("timer try_set_period changes frequency", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    // Set period to 1 ms => 1000 Hz
    auto result = tmr.try_set_period(std::chrono::milliseconds{1});
    TEST_ASSERT_TRUE(result.has_value());

    auto freq = tmr.frequency();
    TEST_ASSERT_INT_WITHIN(50, 1000, static_cast<int>(freq.count()));
}

TEST_CASE("timer unconfigured period returns zero", "[idfxx][pwm][timer]") {
    auto tmr = timer_2;
    TEST_ASSERT_EQUAL(0, tmr.period().count());
    TEST_ASSERT_EQUAL(0, tmr.tick_period().count());
}

TEST_CASE("timer try_pause and try_resume succeed", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto pause_result = tmr.try_pause();
    TEST_ASSERT_TRUE(pause_result.has_value());

    auto resume_result = tmr.try_resume();
    TEST_ASSERT_TRUE(resume_result.has_value());
}

TEST_CASE("timer try_reset succeeds", "[idfxx][pwm][timer]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = tmr.try_reset();
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("try_start with unconfigured timer returns error", "[idfxx][pwm][output]") {
    auto tmr = timer_3;  // not configured
    auto result = try_start(idfxx::gpio_18, tmr, channel::ch_0);
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), result.error().value());
}

TEST_CASE("try_start with NC gpio returns error", "[idfxx][pwm][output]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = try_start(idfxx::gpio::nc(), tmr, channel::ch_0);
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), result.error().value());
}

TEST_CASE("try_start with valid config succeeds", "[idfxx][pwm][output]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = try_start(idfxx::gpio_18, tmr, channel::ch_0);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(channel::ch_0), std::to_underlying(result->channel()));
}

TEST_CASE("output try_set_duty with ratio succeeds", "[idfxx][pwm][output][hw]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = try_start(idfxx::gpio_18, tmr, channel::ch_0);
    TEST_ASSERT_TRUE(result.has_value());

    auto& out = *result;
    auto set_result = out.try_set_duty(0.5f);
    TEST_ASSERT_TRUE(set_result.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, out.duty());
}

TEST_CASE("output try_set_duty_ticks succeeds and reads back", "[idfxx][pwm][output][hw]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = try_start(idfxx::gpio_18, tmr, channel::ch_0);
    TEST_ASSERT_TRUE(result.has_value());

    auto& out = *result;
    auto set_result = out.try_set_duty_ticks(4096);
    TEST_ASSERT_TRUE(set_result.has_value());
    TEST_ASSERT_EQUAL(4096u, out.duty_ticks());
}

TEST_CASE("output try_set_pulse_width succeeds", "[idfxx][pwm][output][hw]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = try_start(idfxx::gpio_18, tmr, channel::ch_0);
    TEST_ASSERT_TRUE(result.has_value());

    auto& out = *result;
    // Set ~50% duty via pulse width (period ~200us, so 100us ~= 50%)
    auto set_result = out.try_set_pulse_width(std::chrono::microseconds{100});
    TEST_ASSERT_TRUE(set_result.has_value());

    // Verify pulse width reads back approximately correctly
    auto pw = out.pulse_width();
    TEST_ASSERT_INT_WITHIN(1000, 100000, static_cast<int>(pw.count()));
}

TEST_CASE("output try_stop succeeds", "[idfxx][pwm][output]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = try_start(idfxx::gpio_18, tmr, channel::ch_0);
    TEST_ASSERT_TRUE(result.has_value());

    auto stop_result = result->try_stop();
    TEST_ASSERT_TRUE(stop_result.has_value());
}

TEST_CASE("output move semantics", "[idfxx][pwm][output][hw]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto result = try_start(idfxx::gpio_18, tmr, channel::ch_0);
    TEST_ASSERT_TRUE(result.has_value());

    // Move construct
    output moved(std::move(*result));
    TEST_ASSERT_EQUAL(std::to_underlying(channel::ch_0), std::to_underlying(moved.channel()));

    // Duty should work on moved-to object
    auto set_result = moved.try_set_duty(0.5f);
    TEST_ASSERT_TRUE(set_result.has_value());
}

TEST_CASE("output release prevents cleanup on destruction", "[idfxx][pwm][output]") {
    auto tmr = timer_0;
    TEST_ASSERT_TRUE(tmr.try_configure({.frequency = 5000_Hz, .resolution_bits = 13}).has_value());

    auto ch = channel::ch_0;
    {
        auto result = try_start(idfxx::gpio_18, tmr, ch);
        TEST_ASSERT_TRUE(result.has_value());
        TEST_ASSERT_TRUE(is_active(ch));

        auto released_ch = result->release();
        TEST_ASSERT_EQUAL(std::to_underlying(ch), std::to_underlying(released_ch));
    }
    // Channel should still be active after output destroyed
    TEST_ASSERT_TRUE(is_active(ch));

    // Clean up via free stop
    auto stop_result = idfxx::pwm::try_stop(ch);
    TEST_ASSERT_TRUE(stop_result.has_value());
    TEST_ASSERT_FALSE(is_active(ch));
}

// =============================================================================
// Auto-allocating try_start tests
// =============================================================================

TEST_CASE("auto try_start with valid config succeeds", "[idfxx][pwm][auto][hw]") {
    auto result = try_start(idfxx::gpio_18, {.frequency = 5000_Hz, .resolution_bits = 13});
    TEST_ASSERT_TRUE(result.has_value());

    auto& out = *result;
    auto set_result = out.try_set_duty(0.5f);
    TEST_ASSERT_TRUE(set_result.has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, out.duty());
}

TEST_CASE("auto try_start with NC gpio returns error", "[idfxx][pwm][auto]") {
    auto result = try_start(idfxx::gpio::nc(), {.frequency = 5000_Hz, .resolution_bits = 13});
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), result.error().value());
}

TEST_CASE("auto try_start reuses matching timer", "[idfxx][pwm][auto]") {
    auto result1 = try_start(idfxx::gpio_18, {.frequency = 5000_Hz, .resolution_bits = 13});
    TEST_ASSERT_TRUE(result1.has_value());

    auto result2 = try_start(idfxx::gpio_19, {.frequency = 5000_Hz, .resolution_bits = 13});
    TEST_ASSERT_TRUE(result2.has_value());

    // Both should be on the same timer
    auto tmr1 = get_timer(result1->channel());
    auto tmr2 = get_timer(result2->channel());
    TEST_ASSERT_TRUE(tmr1.has_value());
    TEST_ASSERT_TRUE(tmr2.has_value());
    TEST_ASSERT_TRUE(*tmr1 == *tmr2);

    // But different channels
    TEST_ASSERT_NOT_EQUAL(std::to_underlying(result1->channel()), std::to_underlying(result2->channel()));
}

TEST_CASE("auto try_start cleanup releases channel for reuse", "[idfxx][pwm][auto]") {
    channel ch;
    {
        auto result = try_start(idfxx::gpio_18, {.frequency = 5000_Hz, .resolution_bits = 13});
        TEST_ASSERT_TRUE(result.has_value());
        ch = result->channel();
        TEST_ASSERT_TRUE(is_active(ch));
    }
    // Channel should be freed after output goes out of scope
    TEST_ASSERT_FALSE(is_active(ch));
}

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(timer) returns expected values", "[idfxx][pwm][to_string]") {
    TEST_ASSERT_EQUAL_STRING("PWM_TIMER_0", idfxx::to_string(timer_0).c_str());
    TEST_ASSERT_EQUAL_STRING("PWM_TIMER_3", idfxx::to_string(timer_3).c_str());
}

TEST_CASE("to_string(channel) returns expected values", "[idfxx][pwm][to_string]") {
    TEST_ASSERT_EQUAL_STRING("PWM_CH_0", idfxx::to_string(channel::ch_0).c_str());
    TEST_ASSERT_EQUAL_STRING("PWM_CH_5", idfxx::to_string(channel::ch_5).c_str());
}

TEST_CASE("to_string(speed_mode) returns expected values", "[idfxx][pwm][to_string]") {
    TEST_ASSERT_EQUAL_STRING("low_speed", idfxx::to_string(speed_mode::low_speed).c_str());
}
