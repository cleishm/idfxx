// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/sched>
 * @file sched.hpp
 * @brief IDFXX scheduling utilities.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_sched Scheduling Utilities
 * @brief Delay and yield functions for task scheduling.
 *
 * Provides scheduling primitives for the current task context:
 * - delay() and delay_until() for time-based suspension
 * - yield() and yield_from_isr() for cooperative scheduling
 * @{
 */

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace idfxx {

/**
 * @brief Delay for the specified duration.
 *
 * Automatically selects the appropriate delay method:
 * - For durations < 10ms: uses `ets_delay_us` (busy-wait, microsecond precision)
 * - For durations >= 10ms: uses `vTaskDelay` (yields to scheduler, tick-based)
 *
 * @tparam Rep The representation type of the duration.
 * @tparam Period The period type of the duration.
 * @param duration The duration to delay for.
 *
 * @note Must not be called from ISR context. Debug builds will assert on ISR context.
 * @note Zero or negative durations return immediately.
 */
template<typename Rep, typename Period>
void delay(const std::chrono::duration<Rep, Period>& duration) {
    delay(std::chrono::ceil<std::chrono::microseconds>(duration));
}

/// @cond INTERNAL
template<>
void delay(const std::chrono::microseconds& duration);
/// @endcond

/**
 * @brief Delays until the specified time point.
 *
 * Computes the remaining time from `Clock::now()` to the target time point
 * and delays for that duration. If the target time has already passed,
 * returns immediately without delaying.
 *
 * This is useful for periodic timing loops where execution time between
 * iterations should not cause drift:
 * @code
 * auto next = idfxx::chrono::tick_clock::now() + 100ms;
 * while (true) {
 *     idfxx::delay_until(next);
 *     next += 100ms;
 *     // Runs every 100ms regardless of execution time
 * }
 * @endcode
 *
 * @tparam Clock The clock type (must provide `Clock::now()`).
 * @tparam Duration The duration type of the time point.
 * @param target The time point to delay until.
 *
 * @note Must not be called from ISR context.
 */
template<typename Clock, typename Duration>
void delay_until(const std::chrono::time_point<Clock, Duration>& target) {
    auto remaining = target - Clock::now();
    if (remaining > decltype(remaining)::zero()) {
        delay(remaining);
    }
}

/**
 * @brief Yields execution to other ready tasks of equal priority.
 */
inline void yield() noexcept {
    taskYIELD();
}

/**
 * @brief Requests a context switch from ISR context.
 *
 * Call this at the end of an ISR when a FreeRTOS API has indicated that
 * a higher priority task was woken. This ensures the scheduler switches
 * to the higher priority task immediately upon ISR completion rather than
 * waiting for the next tick.
 *
 * @param higher_priority_task_woken true if a higher priority task was woken
 *        by a FreeRTOS call during the ISR, false otherwise. When false,
 *        no context switch is requested. Defaults to true for unconditional
 *        context switch.
 *
 * @code
 * void IRAM_ATTR my_isr() {
 *     bool need_yield = worker_task->resume_from_isr();
 *     idfxx::yield_from_isr(need_yield);
 * }
 * @endcode
 */
void yield_from_isr(bool higher_priority_task_woken = true) noexcept;

/** @} */ // end of idfxx_core_sched
/** @} */ // end of idfxx_core

} // namespace idfxx
