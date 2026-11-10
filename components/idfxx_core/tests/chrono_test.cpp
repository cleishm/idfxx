// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx chrono utilities
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/chrono"
#include "idfxx/error"
#include "idfxx/sched"
#include "unity.h"

#include <chrono>
#include <cstdint>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <type_traits>
#include <utility>

using namespace idfxx::chrono;
using namespace std::chrono_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// picoseconds is a signed 64-bit duration finer than nanoseconds
static_assert(std::is_same_v<picoseconds::period, std::pico>);
static_assert(std::is_same_v<picoseconds::rep, int64_t>);
static_assert(picoseconds(std::chrono::nanoseconds(3)) == picoseconds(3000));

// ticks() is constexpr
static_assert(ticks(std::chrono::milliseconds(0)) == 0);

// ticks() returns TickType_t
static_assert(std::is_same_v<decltype(ticks(1ms)), TickType_t>);
static_assert(std::is_same_v<decltype(ticks(1s)), TickType_t>);
static_assert(std::is_same_v<decltype(ticks(std::chrono::microseconds(1))), TickType_t>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("ticks() converts zero duration", "[idfxx][chrono]") {
    TickType_t ticks_0ms = ticks(0ms);
    TEST_ASSERT_EQUAL(0, ticks_0ms);
}

TEST_CASE("ticks() converts milliseconds", "[idfxx][chrono]") {
    // The conversion depends on configTICK_RATE_HZ
    // pdMS_TO_TICKS(1000) should give us the ticks for 1 second
    TickType_t ticks_1s = ticks(1000ms);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(1000), ticks_1s);

    TickType_t ticks_500ms = ticks(500ms);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(500), ticks_500ms);

    TickType_t ticks_100ms = ticks(100ms);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(100), ticks_100ms);
}

TEST_CASE("ticks() converts seconds", "[idfxx][chrono]") {
    TickType_t ticks_1s = ticks(1s);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(1000), ticks_1s);

    TickType_t ticks_5s = ticks(5s);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(5000), ticks_5s);
}

TEST_CASE("ticks() converts minutes", "[idfxx][chrono]") {
    TickType_t ticks_1min = ticks(1min);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(60000), ticks_1min);
}

TEST_CASE("ticks() rounds up sub-millisecond durations", "[idfxx][chrono]") {
    // 1 microsecond should round up to 1 millisecond worth of ticks
    TickType_t ticks_1us = ticks(std::chrono::microseconds(1));
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(1), ticks_1us);

    // 999 microseconds should also round up to 1 millisecond
    TickType_t ticks_999us = ticks(std::chrono::microseconds(999));
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(1), ticks_999us);

    // 1001 microseconds should round up to 2 milliseconds
    TickType_t ticks_1001us = ticks(std::chrono::microseconds(1001));
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(2), ticks_1001us);
}

TEST_CASE("ticks() converts nanoseconds", "[idfxx][chrono]") {
    // Any non-zero nanoseconds should round up to at least 1ms worth of ticks
    TickType_t ticks_1ns = ticks(std::chrono::nanoseconds(1));
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(1), ticks_1ns);

    // 1 million nanoseconds = 1 millisecond exactly
    TickType_t ticks_1ms = ticks(std::chrono::nanoseconds(1000000));
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(1), ticks_1ms);
}

TEST_CASE("ticks() handles chrono literals", "[idfxx][chrono]") {
    // Test various chrono literals work correctly
    auto ticks_ms = ticks(250ms);
    auto ticks_s = ticks(2s);

    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(250), ticks_ms);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(2000), ticks_s);
}

TEST_CASE("ticks() handles duration arithmetic", "[idfxx][chrono]") {
    auto duration = 1s + 500ms;
    TickType_t ticks_duration = ticks(duration);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(1500), ticks_duration);
}

TEST_CASE("ticks() with hours", "[idfxx][chrono]") {
    TickType_t ticks_1h = ticks(1h);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(3600000), ticks_1h);
}

// =============================================================================
// Clocks
// =============================================================================

// Both clocks satisfy the C++20 Clock requirements
static_assert(std::chrono::is_clock_v<tick_clock>);
static_assert(std::chrono::is_clock_v<rtc_clock>);

// rtc_clock counts microseconds in a signed 64-bit representation
static_assert(std::is_same_v<rtc_clock::duration, std::chrono::microseconds>);
static_assert(std::is_same_v<rtc_clock::period, std::micro>);
static_assert(std::is_signed_v<rtc_clock::rep>);
static_assert(sizeof(rtc_clock::rep) >= 8);
static_assert(rtc_clock::is_steady);
static_assert(std::is_same_v<decltype(rtc_clock::now()), rtc_clock::time_point>);
static_assert(noexcept(rtc_clock::now()));

// A time_point is constant-initialized, so it can live in RTC memory and survive deep sleep
RTC_DATA_ATTR constinit static rtc_clock::time_point rtc_saved_time_point;

TEST_CASE("rtc_clock::now() has advanced since power-on", "[idfxx][chrono]") {
    TEST_ASSERT_TRUE(rtc_clock::now().time_since_epoch() > rtc_clock::duration::zero());
}

TEST_CASE("rtc_clock::now() never runs backward", "[idfxx][chrono]") {
    auto previous = rtc_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto current = rtc_clock::now();
        TEST_ASSERT_TRUE(current >= previous);
        previous = current;
    }
}

TEST_CASE("rtc_clock tracks steady_clock", "[idfxx][chrono]") {
    auto rtc_start = rtc_clock::now();
    auto steady_start = std::chrono::steady_clock::now();
    idfxx::delay(200ms);
    auto rtc_elapsed = rtc_clock::now() - rtc_start;
    auto steady_elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - steady_start);

    // The RTC slow clock is calibrated at boot but is not a precision source, and QEMU's
    // emulation of it runs about 10% fast. The tolerance only needs to catch scale errors.
    auto rtc_us = static_cast<int32_t>(rtc_elapsed.count());
    auto steady_us = static_cast<int32_t>(steady_elapsed.count());
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(150'000, rtc_us);
    TEST_ASSERT_INT32_WITHIN(steady_us / 4, steady_us, rtc_us);
}

TEST_CASE("rtc_clock time_point round-trips through RTC memory", "[idfxx][chrono]") {
    auto now = rtc_clock::now();
    rtc_saved_time_point = now;
    TEST_ASSERT_TRUE(rtc_saved_time_point == now);
    TEST_ASSERT_TRUE(rtc_clock::now() - rtc_saved_time_point >= rtc_clock::duration::zero());
}

// =============================================================================
// System time
// =============================================================================

// set_system_time accepts any sys_time duration
static_assert(std::is_same_v<decltype(try_set_system_time(std::chrono::sys_seconds{})), idfxx::result<void>>);
static_assert(std::is_same_v<
              decltype(try_set_system_time(std::chrono::sys_time<std::chrono::nanoseconds>{})),
              idfxx::result<void>>);

TEST_CASE("try_set_system_time() sets system_clock", "[idfxx][chrono]") {
    using namespace std::chrono;
    const auto original = system_clock::now();
    const auto steady_start = steady_clock::now();

    // 2024-02-29T12:34:56.789
    const auto target = sys_days{2024y / February / 29} + 12h + 34min + 56s + 789ms;
    TEST_ASSERT_TRUE(try_set_system_time(target).has_value());
    const auto now = system_clock::now();

    // Restore the original time, adjusted for the time spent in this test
    TEST_ASSERT_TRUE(try_set_system_time(original + (steady_clock::now() - steady_start)).has_value());

    const auto diff_ms = static_cast<int32_t>(duration_cast<milliseconds>(now - target).count());
    TEST_ASSERT_INT32_WITHIN(1000, 0, diff_ms);
}

TEST_CASE("try_set_system_time() rejects times before 1970", "[idfxx][chrono]") {
    using namespace std::chrono;
    const auto before = system_clock::now();

    auto r = try_set_system_time(sys_days{1970y / January / 1} - 1us);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), r.error().value());

    // The system time is left unchanged
    const auto diff_ms = static_cast<int32_t>(duration_cast<milliseconds>(system_clock::now() - before).count());
    TEST_ASSERT_INT32_WITHIN(1000, 0, diff_ms);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("set_system_time() sets system_clock", "[idfxx][chrono]") {
    using namespace std::chrono;
    const auto original = system_clock::now();
    const auto steady_start = steady_clock::now();

    // 2099-12-31T23:59:59
    const auto target = sys_days{2099y / December / 31} + 23h + 59min + 59s;
    set_system_time(target);
    const auto now = system_clock::now();

    set_system_time(original + (steady_clock::now() - steady_start));

    const auto diff_ms = static_cast<int32_t>(duration_cast<milliseconds>(now - target).count());
    TEST_ASSERT_INT32_WITHIN(1000, 0, diff_ms);
}
#endif
