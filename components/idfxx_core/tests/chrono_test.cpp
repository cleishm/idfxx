// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx chrono utilities
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/chrono"
#include "unity.h"

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <type_traits>

using namespace idfxx::chrono;
using namespace std::chrono_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

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
