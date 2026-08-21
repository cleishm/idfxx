// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "idfxx/sched"

#include <idfxx/chrono>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>

namespace {

using tick_duration = idfxx::chrono::tick_clock::duration;

// One scheduler tick: the finest delay vTaskDelay() can express.
constexpr auto tick_period = std::chrono::ceil<std::chrono::microseconds>(tick_duration{1});

[[nodiscard]] bool in_isr_context() noexcept {
    return xPortInIsrContext() != 0;
}

// Ticks to ask vTaskDelay() for so that it does not wake before `remaining` has
// elapsed: rounded up, and clamped to what TickType_t can carry (the caller loops).
[[nodiscard]] TickType_t ticks_for(std::chrono::microseconds remaining) noexcept {
    const int64_t ticks = (remaining.count() + tick_period.count() - 1) / tick_period.count();
    return static_cast<TickType_t>(std::min<int64_t>(ticks, portMAX_DELAY));
}

void busy_wait(std::chrono::microseconds duration) noexcept {
    auto us = duration.count();
    // ets_delay_us takes a uint32_t; split very long waits
    while (us > UINT32_MAX) {
        ets_delay_us(UINT32_MAX);
        us -= UINT32_MAX;
    }
    if (us > 0) {
        ets_delay_us(static_cast<uint32_t>(us));
    }
}

} // anonymous namespace

namespace idfxx {

template<>
void delay(const std::chrono::microseconds& duration) {
    if (duration <= std::chrono::microseconds::zero()) {
        return;
    }

    assert(!in_isr_context() && "delay() must not be called from ISR context");

    if (duration < tick_period) {
        // The scheduler cannot resolve sub-tick durations; spin for precision.
        busy_wait(duration);
        return;
    }

    // vTaskDelay(n) wakes on the n-th tick interrupt after the call, which can
    // arrive almost a full tick before n tick periods have elapsed. Re-check the
    // clock and top up until the deadline has passed, so the delay is never short.
    const auto deadline = std::chrono::steady_clock::now() + duration;
    auto remaining = duration;
    while (remaining > std::chrono::microseconds::zero()) {
        vTaskDelay(ticks_for(remaining));
        remaining = std::chrono::ceil<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now());
    }
}

void delay(next_tick_t) noexcept {
    assert(!in_isr_context() && "delay(next_tick) must not be called from ISR context");
    vTaskDelay(1);
}

void IRAM_ATTR yield_from_isr(bool higher_priority_task_woken) noexcept {
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

} // namespace idfxx
