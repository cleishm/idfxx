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
 * - delay(next_tick) for the shortest blocking delay, as the back-off in polling loops
 * - yield() and yield_from_isr() for cooperative scheduling
 * @{
 */

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace idfxx {

/**
 * @brief Delays the calling task for at least the specified duration.
 *
 * How the delay is carried out depends on its length relative to the scheduler
 * tick period (`1 / configTICK_RATE_HZ`; 10 ms at the default 100 Hz):
 *
 * - Durations of one tick or more block the task, so other tasks run in the
 *   meantime. The task wakes on a tick boundary, so the delay is never shorter
 *   than requested but may overshoot by up to one tick.
 * - Durations shorter than one tick cannot be scheduled, so they are busy-waited
 *   with microsecond precision. The CPU spins for the whole duration: no task of
 *   equal or lower priority runs, and the delay is accurate to within a few
 *   microseconds.
 *
 * @warning Sub-tick delays are busy-waits. Do not use a short delay() as the
 *   back-off in a polling loop: it burns the CPU and starves lower-priority
 *   tasks, including the idle task that feeds the task watchdog. Use
 *   `delay(next_tick)` instead, which always blocks until the next tick.
 *
 * @tparam Rep The representation type of the duration.
 * @tparam Period The period type of the duration.
 * @param duration The minimum duration to delay for.
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
 * and delays for that duration (see delay() for how sub-tick and longer
 * remainders are handled). If the target time has already passed, returns
 * immediately without delaying.
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
 * @headerfile <idfxx/sched>
 * @brief Tag type selecting the delay-until-next-tick overload of delay().
 *
 * Use the `next_tick` constant rather than constructing this directly.
 */
struct next_tick_t {
    explicit next_tick_t() = default;
};

/**
 * @brief Tag requesting a delay until the next scheduler tick: `idfxx::delay(idfxx::next_tick)`.
 */
inline constexpr next_tick_t next_tick{};

/**
 * @brief Blocks the calling task until the next scheduler tick.
 *
 * The shortest delay that gives up the CPU. The task is suspended until the
 * next tick interrupt — anywhere from a moment to one full tick period
 * (`1 / configTICK_RATE_HZ`; 10 ms at the default 100 Hz) — and every other
 * ready task, whatever its priority, may run in the meantime. Use it as the
 * back-off in polling loops:
 *
 * @code
 * while (busy_pin.get_level() == idfxx::gpio::level::high) {
 *     idfxx::delay(idfxx::next_tick);
 * }
 * @endcode
 *
 * Unlike yield(), which hands the CPU only to ready tasks of equal priority
 * and returns at once when there are none, `delay(next_tick)` always blocks,
 * so a loop built on it cannot starve lower-priority tasks or the idle task.
 * Unlike a short duration-based delay(), it never busy-waits.
 *
 * @param tag The `next_tick` tag.
 *
 * @note Must not be called from ISR context. Debug builds will assert on ISR context.
 */
void delay(next_tick_t tag) noexcept;

/**
 * @brief Yields execution to other ready tasks of equal priority.
 *
 * Requests an immediate context switch without blocking. If no task of equal
 * or higher priority is ready, the calling task simply continues; lower-priority
 * tasks never run as a result of a yield. For a scheduler-friendly back-off in
 * a polling loop, use `delay(next_tick)` instead.
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
