// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx scheduling utilities
// Uses ESP-IDF Unity test framework

#include "idfxx/chrono"
#include "idfxx/sched"
#include "unity.h"

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <utility>

using namespace std::chrono_literals;

namespace {

// One scheduler tick as a std::chrono duration (10ms at the default 100Hz).
constexpr auto tick = std::chrono::ceil<std::chrono::microseconds>(idfxx::chrono::tick_clock::duration{1});

// Wall-clock time spent in `fn`, and the number of scheduler ticks that passed.
template<typename Fn>
auto measure(Fn&& fn) {
    const auto start_ticks = xTaskGetTickCount();
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsed_ticks = xTaskGetTickCount() - start_ticks;
    return std::pair{elapsed, elapsed_ticks};
}

} // namespace

// =============================================================================
// Delay tests
// =============================================================================

TEST_CASE("delay() with zero duration returns immediately", "[idfxx][sched]") {
    auto [elapsed, ticks] = measure([] { idfxx::delay(0ms); });
    TEST_ASSERT_EQUAL(0, ticks);
}

TEST_CASE("delay() with negative duration returns immediately", "[idfxx][sched]") {
    auto [elapsed, ticks] = measure([] { idfxx::delay(-100ms); });
    TEST_ASSERT_EQUAL(0, ticks);
}

TEST_CASE("delay() busy-waits sub-tick durations", "[idfxx][sched][hw]") {
    // Half a tick cannot be scheduled, so it spins: accurate, and never as long as a full tick
    // Note: Requires precise hardware timing - QEMU timing is not accurate enough
    auto [elapsed, ticks] = measure([] { idfxx::delay(tick / 2); });
    TEST_ASSERT_GREATER_OR_EQUAL(tick.count() / 2, std::chrono::ceil<std::chrono::microseconds>(elapsed).count());
    TEST_ASSERT_LESS_OR_EQUAL(1, ticks);
}

TEST_CASE("delay() works with microseconds", "[idfxx][sched][hw]") {
    // 500 microseconds is sub-tick at any supported tick rate, so it busy-waits
    // Note: Requires precise hardware timing - QEMU timing is not accurate enough
    auto [elapsed, ticks] = measure([] { idfxx::delay(500us); });
    TEST_ASSERT_GREATER_OR_EQUAL(500, std::chrono::ceil<std::chrono::microseconds>(elapsed).count());
    TEST_ASSERT_LESS_OR_EQUAL(1, ticks);
}

TEST_CASE("delay() of one tick blocks for at least one tick", "[idfxx][sched]") {
    // Exactly one tick is the shortest scheduled (non-spinning) delay. vTaskDelay(1) alone can
    // return almost immediately; delay() must top it up so the full tick period elapses.
    auto [elapsed, ticks] = measure([] { idfxx::delay(tick); });
    TEST_ASSERT_GREATER_OR_EQUAL(tick.count(), std::chrono::ceil<std::chrono::microseconds>(elapsed).count());
    TEST_ASSERT_GREATER_OR_EQUAL(1, ticks);
}

TEST_CASE("delay() never returns before the requested duration", "[idfxx][sched]") {
    // A spread of scheduled delays, including ones that are not whole ticks
    for (const auto requested : {tick, tick + tick / 2, 3 * tick, std::chrono::microseconds(50ms),
             std::chrono::microseconds(55ms)}) {
        auto [elapsed, ticks] = measure([&] { idfxx::delay(requested); });
        TEST_ASSERT_GREATER_OR_EQUAL(requested.count(), std::chrono::ceil<std::chrono::microseconds>(elapsed).count());
    }
}

TEST_CASE("delay() overshoots by at most one tick", "[idfxx][sched][hw]") {
    // Note: Requires precise hardware timing - QEMU timing is not accurate enough
    auto [elapsed, ticks] = measure([] { idfxx::delay(50ms); });
    const auto limit = 50ms + tick;
    TEST_ASSERT_LESS_OR_EQUAL(limit.count(), std::chrono::ceil<std::chrono::microseconds>(elapsed).count());
}

TEST_CASE("delay() uses the scheduler for longer delays", "[idfxx][sched]") {
    auto [elapsed, ticks] = measure([] { idfxx::delay(50ms); });

    // Should take about pdMS_TO_TICKS(50) ticks; allow the one-tick overshoot plus some
    // scheduling variance, which rules out a busy-wait only in the sense that the tick
    // count advanced as expected while we were blocked
    TickType_t expected = pdMS_TO_TICKS(50);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, ticks);
    TEST_ASSERT_LESS_OR_EQUAL(expected + 5, ticks);
}

TEST_CASE("delay() works with seconds", "[idfxx][sched]") {
    // 100ms expressed in a different unit
    auto [elapsed, ticks] = measure([] { idfxx::delay(std::chrono::duration<double>(0.1)); });

    TickType_t expected = pdMS_TO_TICKS(100);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, ticks);
    TEST_ASSERT_LESS_OR_EQUAL(expected + 2, ticks);
}

TEST_CASE("delay() works with chrono literals", "[idfxx][sched]") {
    auto [elapsed, ticks] = measure([] { idfxx::delay(20ms); });

    TickType_t expected = pdMS_TO_TICKS(20);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, ticks);
}

TEST_CASE("delay() works with duration arithmetic", "[idfxx][sched]") {
    auto duration = 15ms + 5ms; // 20ms total
    auto [elapsed, ticks] = measure([&] { idfxx::delay(duration); });

    TickType_t expected = pdMS_TO_TICKS(20);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, ticks);
}

// =============================================================================
// delay_until tests
// =============================================================================

TEST_CASE("delay_until() with past time point returns immediately", "[idfxx][sched]") {
    // A time point in the past should not delay at all
    auto past = idfxx::chrono::tick_clock::now() - std::chrono::milliseconds(100);
    auto [elapsed, ticks] = measure([&] { idfxx::delay_until(past); });
    TEST_ASSERT_EQUAL(0, ticks);
}

TEST_CASE("delay_until() with future time point delays", "[idfxx][sched]") {
    auto target = idfxx::chrono::tick_clock::now() + std::chrono::milliseconds(50);
    auto [elapsed, ticks] = measure([&] { idfxx::delay_until(target); });

    TickType_t expected = pdMS_TO_TICKS(50);
    TEST_ASSERT_GREATER_OR_EQUAL(expected, ticks);
    TEST_ASSERT_LESS_OR_EQUAL(expected + 5, ticks);
    TEST_ASSERT_TRUE(idfxx::chrono::tick_clock::now() >= target);
}

TEST_CASE("delay_until() with current time returns immediately", "[idfxx][sched]") {
    auto now = idfxx::chrono::tick_clock::now();
    auto [elapsed, ticks] = measure([&] { idfxx::delay_until(now); });
    TEST_ASSERT_LESS_OR_EQUAL(1, ticks);
}

// =============================================================================
// delay(next_tick) tests
// =============================================================================

TEST_CASE("delay(next_tick) blocks until the next tick", "[idfxx][sched]") {
    for (int i = 0; i < 5; ++i) {
        auto [elapsed, ticks] = measure([] { idfxx::delay(idfxx::next_tick); });
        // Wakes on the next tick interrupt, so the tick count always advances
        TEST_ASSERT_GREATER_OR_EQUAL(1, ticks);
    }
}

TEST_CASE("delay(next_tick) never exceeds one tick period", "[idfxx][sched][hw]") {
    // Note: Requires precise hardware timing - QEMU timing is not accurate enough
    auto [elapsed, ticks] = measure([] { idfxx::delay(idfxx::next_tick); });
    const auto limit = tick + 1ms; // tick period plus interrupt and scheduling latency
    TEST_ASSERT_LESS_OR_EQUAL(limit.count(), std::chrono::floor<std::chrono::microseconds>(elapsed).count());
}

namespace {

struct helper_state {
    volatile bool ran = false;
};

void helper_task(void* arg) {
    static_cast<helper_state*>(arg)->ran = true;
    vTaskDelete(nullptr);
}

} // namespace

TEST_CASE("delay(next_tick) lets lower-priority tasks run where yield() does not", "[idfxx][sched]") {
    helper_state state;

    // Run above a helper that is itself above idle, pinned to this core so the helper can only
    // run when this task gives up the CPU
    const auto prio = uxTaskPriorityGet(nullptr);
    vTaskPrioritySet(nullptr, prio + 2);
    const auto created = xTaskCreatePinnedToCore(helper_task, "sched_helper", 2048, &state, prio + 1, nullptr,
        xPortGetCoreID());

    idfxx::yield();
    const bool ran_after_yield = state.ran;

    idfxx::delay(idfxx::next_tick);
    const bool ran_after_delay_tick = state.ran;

    vTaskPrioritySet(nullptr, prio);
    idfxx::delay(idfxx::next_tick); // let idle reclaim the helper's resources before asserting

    TEST_ASSERT_EQUAL(pdPASS, created);
    TEST_ASSERT_FALSE(ran_after_yield);
    TEST_ASSERT_TRUE(ran_after_delay_tick);
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
