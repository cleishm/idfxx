// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx scheduling utilities
// Uses ESP-IDF Unity test framework

#include "idfxx/sched"
#include "unity.h"

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace std::chrono_literals;

// =============================================================================
// Delay tests
// =============================================================================

TEST_CASE("delay() with zero duration returns immediately", "[idfxx][sched]") {
    auto start = xTaskGetTickCount();
    idfxx::delay(0ms);
    auto elapsed = xTaskGetTickCount() - start;
    TEST_ASSERT_EQUAL(0, elapsed);
}

TEST_CASE("delay() with negative duration returns immediately", "[idfxx][sched]") {
    auto start = xTaskGetTickCount();
    idfxx::delay(-100ms);
    auto elapsed = xTaskGetTickCount() - start;
    TEST_ASSERT_EQUAL(0, elapsed);
}

TEST_CASE("delay() uses busy-wait for short delays", "[idfxx][sched][hw]") {
    // 5ms should use busy-wait (< 10ms threshold)
    // With busy-wait, the delay should be accurate to microseconds
    // Note: Requires precise hardware timing - QEMU timing is not accurate enough
    auto start = xTaskGetTickCount();
    idfxx::delay(5ms);
    auto elapsed = xTaskGetTickCount() - start;

    // Busy-wait for 5ms should complete within 0-1 ticks depending on tick rate
    // At 100Hz, a tick is 10ms, so 5ms busy-wait should be < 1 tick elapsed
    TEST_ASSERT_LESS_OR_EQUAL(1, elapsed);
}

TEST_CASE("delay() uses vTaskDelay for longer delays", "[idfxx][sched][hw]") {
    // 50ms should use vTaskDelay (>= 10ms threshold)
    // Note: Requires precise hardware timing - QEMU timing is not accurate enough
    auto start = xTaskGetTickCount();
    idfxx::delay(50ms);
    auto elapsed = xTaskGetTickCount() - start;

    // vTaskDelay should take approximately pdMS_TO_TICKS(50) ticks
    // Allow some tolerance for scheduling variance
    TickType_t expected = pdMS_TO_TICKS(50);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, elapsed);
    TEST_ASSERT_LESS_OR_EQUAL(expected + 5, elapsed);
}

TEST_CASE("delay() works at threshold boundary", "[idfxx][sched]") {
    // Exactly 10ms should use vTaskDelay
    auto start = xTaskGetTickCount();
    idfxx::delay(10ms);
    auto elapsed = xTaskGetTickCount() - start;

    // Should use vTaskDelay path
    TickType_t expected = pdMS_TO_TICKS(10);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, elapsed);
}

TEST_CASE("delay() works with microseconds", "[idfxx][sched][hw]") {
    // 500 microseconds should use busy-wait
    // Note: Requires precise hardware timing - QEMU timing is not accurate enough
    auto start = xTaskGetTickCount();
    idfxx::delay(std::chrono::microseconds(500));
    auto elapsed = xTaskGetTickCount() - start;

    // Should complete within a single tick
    TEST_ASSERT_LESS_OR_EQUAL(1, elapsed);
}

TEST_CASE("delay() works with seconds", "[idfxx][sched]") {
    // 100ms expressed in a different unit
    auto start = xTaskGetTickCount();
    idfxx::delay(std::chrono::milliseconds(100));
    auto elapsed = xTaskGetTickCount() - start;

    TickType_t expected = pdMS_TO_TICKS(100);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, elapsed);
    TEST_ASSERT_LESS_OR_EQUAL(expected + 2, elapsed);
}

TEST_CASE("delay() works with chrono literals", "[idfxx][sched]") {
    auto start = xTaskGetTickCount();
    idfxx::delay(20ms);
    auto elapsed = xTaskGetTickCount() - start;

    TickType_t expected = pdMS_TO_TICKS(20);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, elapsed);
}

TEST_CASE("delay() works with duration arithmetic", "[idfxx][sched]") {
    auto duration = 15ms + 5ms; // 20ms total
    auto start = xTaskGetTickCount();
    idfxx::delay(duration);
    auto elapsed = xTaskGetTickCount() - start;

    TickType_t expected = pdMS_TO_TICKS(20);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, elapsed);
}

// =============================================================================
// Yield tests
// =============================================================================

TEST_CASE("yield does not crash", "[idfxx][sched]") {
    // Just verify yield doesn't cause issues
    idfxx::yield();
    idfxx::yield();
    idfxx::yield();
    // If we get here, it worked
}

TEST_CASE("yield_from_isr with false does not crash", "[idfxx][sched]") {
    // Calling with false should be a no-op (no context switch requested)
    idfxx::yield_from_isr(false);
    // If we get here, it worked
}

TEST_CASE("yield_from_isr no-arg does not crash", "[idfxx][sched]") {
    // No-arg version unconditionally requests a context switch
    idfxx::yield_from_isr();
    // If we get here, it worked
}
