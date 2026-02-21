// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/event_group>
 * @file event_group.hpp
 * @brief Type-safe inter-task event group synchronization.
 *
 * @defgroup idfxx_event_group Event Group Component
 * @brief Type-safe event group for inter-task synchronization.
 *
 * Provides a type-safe event group with support for setting, clearing, waiting,
 * and synchronizing on event bits. Uses `idfxx::flags<E>` for type-safe bit
 * manipulation, ISR operations, and dual error handling (exception-based and
 * result-based APIs).
 *
 * Depends on @ref idfxx_core for error handling, chrono utilities, and flags.
 * @{
 */

#include <idfxx/chrono>
#include <idfxx/error>
#include <idfxx/flags>

#include <chrono>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <type_traits>

namespace idfxx {

/**
 * @headerfile <idfxx/event_group>
 * @brief Specifies whether to wait for any or all of the requested bits.
 */
enum class wait_mode {
    any, ///< Wait for any of the specified bits to be set.
    all, ///< Wait for all of the specified bits to be set.
};

/**
 * @headerfile <idfxx/event_group>
 * @brief Type-safe inter-task event group for bit-level synchronization.
 *
 * A fixed set of event bits that can be set, cleared, and waited upon across
 * tasks and ISRs. Event bits are represented as a `flags<E>` value, providing
 * full type safety.
 *
 * Event groups are non-copyable and non-movable.
 *
 * @tparam E The flag enum type (must satisfy flag_enum concept). The underlying
 *           type must fit within EventBits_t.
 *
 * @code
 * enum class my_event : uint32_t {
 *     data_ready = 1u << 0,
 *     timeout    = 1u << 1,
 *     error      = 1u << 2,
 * };
 * template<> inline constexpr bool idfxx::enable_flags_operators<my_event> = true;
 *
 * // Create an event group
 * idfxx::event_group<my_event> eg;
 *
 * // Set bits from one task
 * eg.set(my_event::data_ready);
 *
 * // Wait for bits in another task
 * auto bits = eg.wait(my_event::data_ready, idfxx::wait_mode::any);
 * @endcode
 */
template<flag_enum E>
    requires(sizeof(std::underlying_type_t<E>) <= sizeof(EventBits_t))
class event_group {
public:
    /**
     * @brief Creates an event group.
     *
     * @throws std::bad_alloc if memory allocation fails.
     */
    [[nodiscard]] event_group()
        : _handle(xEventGroupCreate()) {
        if (_handle == nullptr) {
            raise_no_mem();
        }
    }

    /**
     * @brief Destroys the event group and releases all resources.
     *
     * Any tasks blocked waiting on this event group are unblocked and receive
     * a return value of zero.
     */
    ~event_group() {
        if (_handle != nullptr) {
            vEventGroupDelete(_handle);
        }
    }

    // Non-copyable and non-movable
    event_group(const event_group&) = delete;
    event_group& operator=(const event_group&) = delete;
    event_group(event_group&&) = delete;
    event_group& operator=(event_group&&) = delete;

    // =========================================================================
    // Set operations
    // =========================================================================

    /**
     * @brief Sets event bits in the event group.
     *
     * Sets the specified bits. Any tasks waiting for these bits are unblocked
     * if their wait condition is now satisfied.
     *
     * This operation always succeeds and never blocks.
     *
     * @param bits The bits to set.
     * @return The event group bits at the time this call returns. The returned
     *         value may have bits cleared by tasks that were unblocked.
     */
    flags<E> set(flags<E> bits) noexcept {
        return flags<E>::from_raw(static_cast<std::underlying_type_t<E>>(xEventGroupSetBits(_handle, bits.value())));
    }

    // =========================================================================
    // Clear operations
    // =========================================================================

    /**
     * @brief Clears event bits in the event group.
     *
     * Clears the specified bits. This operation always succeeds and never blocks.
     *
     * @param bits The bits to clear.
     * @return The event group bits before clearing.
     */
    flags<E> clear(flags<E> bits) noexcept {
        return flags<E>::from_raw(static_cast<std::underlying_type_t<E>>(xEventGroupClearBits(_handle, bits.value())));
    }

    // =========================================================================
    // Get operations
    // =========================================================================

    /**
     * @brief Returns the current event bits.
     *
     * @return The current event group bits.
     */
    [[nodiscard]] flags<E> get() const noexcept {
        return flags<E>::from_raw(static_cast<std::underlying_type_t<E>>(xEventGroupGetBits(_handle)));
    }

    // =========================================================================
    // Wait operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Waits for event bits to be set, blocking indefinitely.
     *
     * Blocks until the wait condition is satisfied.
     *
     * @param bits The bits to wait for.
     * @param mode Whether to wait for any or all of the specified bits.
     * @param clear_on_exit If true, the waited-for bits are cleared before returning.
     * @return The event group bits at the time the wait condition was satisfied.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the wait condition
     *         is not satisfied.
     */
    [[nodiscard]] flags<E> wait(flags<E> bits, wait_mode mode, bool clear_on_exit = true) {
        return unwrap(try_wait(bits, mode, clear_on_exit));
    }

    /**
     * @brief Waits for event bits to be set, with a timeout.
     *
     * Blocks until the wait condition is satisfied or the timeout expires.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param bits The bits to wait for.
     * @param mode Whether to wait for any or all of the specified bits.
     * @param timeout Maximum time to wait.
     * @param clear_on_exit If true, the waited-for bits are cleared before returning.
     * @return The event group bits at the time the wait condition was satisfied.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the wait condition
     *         is not satisfied within the timeout.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] flags<E>
    wait(flags<E> bits, wait_mode mode, const std::chrono::duration<Rep, Period>& timeout, bool clear_on_exit = true) {
        return unwrap(try_wait(bits, mode, timeout, clear_on_exit));
    }

    /**
     * @brief Waits for event bits to be set, with a deadline.
     *
     * Blocks until the wait condition is satisfied or the deadline is reached.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param bits The bits to wait for.
     * @param mode Whether to wait for any or all of the specified bits.
     * @param deadline The time point at which to stop waiting.
     * @param clear_on_exit If true, the waited-for bits are cleared before returning.
     * @return The event group bits at the time the wait condition was satisfied.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the wait condition
     *         is not satisfied before the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] flags<E> wait_until(
        flags<E> bits,
        wait_mode mode,
        const std::chrono::time_point<Clock, Duration>& deadline,
        bool clear_on_exit = true
    ) {
        return unwrap(try_wait_until(bits, mode, deadline, clear_on_exit));
    }
#endif

    /**
     * @brief Waits for event bits to be set, blocking indefinitely.
     *
     * Blocks until the wait condition is satisfied.
     *
     * @param bits The bits to wait for.
     * @param mode Whether to wait for any or all of the specified bits.
     * @param clear_on_exit If true, the waited-for bits are cleared before returning.
     * @return The event group bits at the time the wait condition was satisfied, or an error.
     * @retval timeout The wait condition was not satisfied.
     */
    [[nodiscard]] result<flags<E>> try_wait(flags<E> bits, wait_mode mode, bool clear_on_exit = true) {
        return _try_wait(bits, mode, clear_on_exit, portMAX_DELAY);
    }

    /**
     * @brief Waits for event bits to be set, with a timeout.
     *
     * Blocks until the wait condition is satisfied or the timeout expires.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param bits The bits to wait for.
     * @param mode Whether to wait for any or all of the specified bits.
     * @param timeout Maximum time to wait.
     * @param clear_on_exit If true, the waited-for bits are cleared before returning.
     * @return The event group bits at the time the wait condition was satisfied, or an error.
     * @retval timeout The wait condition was not satisfied within the timeout.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<flags<E>> try_wait(
        flags<E> bits,
        wait_mode mode,
        const std::chrono::duration<Rep, Period>& timeout,
        bool clear_on_exit = true
    ) {
        return _try_wait(bits, mode, clear_on_exit, chrono::ticks(timeout));
    }

    /**
     * @brief Waits for event bits to be set, with a deadline.
     *
     * Blocks until the wait condition is satisfied or the deadline is reached.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param bits The bits to wait for.
     * @param mode Whether to wait for any or all of the specified bits.
     * @param deadline The time point at which to stop waiting.
     * @param clear_on_exit If true, the waited-for bits are cleared before returning.
     * @return The event group bits at the time the wait condition was satisfied, or an error.
     * @retval timeout The wait condition was not satisfied before the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] result<flags<E>> try_wait_until(
        flags<E> bits,
        wait_mode mode,
        const std::chrono::time_point<Clock, Duration>& deadline,
        bool clear_on_exit = true
    ) {
        auto remaining = deadline - Clock::now();
        if (remaining <= decltype(remaining)::zero()) {
            return _try_wait(bits, mode, clear_on_exit, 0);
        }
        return _try_wait(bits, mode, clear_on_exit, chrono::ticks(remaining));
    }

    // =========================================================================
    // Sync operations
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Atomically sets bits and waits for other bits, blocking indefinitely.
     *
     * Sets the specified bits and then waits for all of the wait bits to be set.
     * This is used for task rendezvous — multiple tasks each set their own bit
     * and wait for all bits to be set.
     *
     * The bits set by this call and the bits waited for are automatically cleared
     * before the function returns.
     *
     * @param set_bits The bits to set before waiting.
     * @param wait_bits The bits to wait for (all must be set).
     * @return The event group bits at the time all wait bits were set (before clearing).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the wait condition
     *         is not satisfied.
     */
    [[nodiscard]] flags<E> sync(flags<E> set_bits, flags<E> wait_bits) { return unwrap(try_sync(set_bits, wait_bits)); }

    /**
     * @brief Atomically sets bits and waits for other bits, with a timeout.
     *
     * Sets the specified bits and then waits for all of the wait bits to be set.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param set_bits The bits to set before waiting.
     * @param wait_bits The bits to wait for (all must be set).
     * @param timeout Maximum time to wait.
     * @return The event group bits at the time all wait bits were set (before clearing).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the wait condition
     *         is not satisfied within the timeout.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] flags<E>
    sync(flags<E> set_bits, flags<E> wait_bits, const std::chrono::duration<Rep, Period>& timeout) {
        return unwrap(try_sync(set_bits, wait_bits, timeout));
    }

    /**
     * @brief Atomically sets bits and waits for other bits, with a deadline.
     *
     * Sets the specified bits and then waits for all of the wait bits to be set.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param set_bits The bits to set before waiting.
     * @param wait_bits The bits to wait for (all must be set).
     * @param deadline The time point at which to stop waiting.
     * @return The event group bits at the time all wait bits were set (before clearing).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error with idfxx::errc::timeout if the wait condition
     *         is not satisfied before the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] flags<E>
    sync_until(flags<E> set_bits, flags<E> wait_bits, const std::chrono::time_point<Clock, Duration>& deadline) {
        return unwrap(try_sync_until(set_bits, wait_bits, deadline));
    }
#endif

    /**
     * @brief Atomically sets bits and waits for other bits, blocking indefinitely.
     *
     * Sets the specified bits and then waits for all of the wait bits to be set.
     * This is used for task rendezvous — multiple tasks each set their own bit
     * and wait for all bits to be set.
     *
     * The bits set by this call and the bits waited for are automatically cleared
     * before the function returns.
     *
     * @param set_bits The bits to set before waiting.
     * @param wait_bits The bits to wait for (all must be set).
     * @return The event group bits at the time all wait bits were set (before clearing), or an error.
     * @retval timeout The wait condition was not satisfied.
     */
    [[nodiscard]] result<flags<E>> try_sync(flags<E> set_bits, flags<E> wait_bits) {
        return _try_sync(set_bits, wait_bits, portMAX_DELAY);
    }

    /**
     * @brief Atomically sets bits and waits for other bits, with a timeout.
     *
     * Sets the specified bits and then waits for all of the wait bits to be set.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param set_bits The bits to set before waiting.
     * @param wait_bits The bits to wait for (all must be set).
     * @param timeout Maximum time to wait.
     * @return The event group bits at the time all wait bits were set (before clearing), or an error.
     * @retval timeout The wait condition was not satisfied within the timeout.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<flags<E>>
    try_sync(flags<E> set_bits, flags<E> wait_bits, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_sync(set_bits, wait_bits, chrono::ticks(timeout));
    }

    /**
     * @brief Atomically sets bits and waits for other bits, with a deadline.
     *
     * Sets the specified bits and then waits for all of the wait bits to be set.
     *
     * @tparam Clock The clock type.
     * @tparam Duration The duration type of the time point.
     * @param set_bits The bits to set before waiting.
     * @param wait_bits The bits to wait for (all must be set).
     * @param deadline The time point at which to stop waiting.
     * @return The event group bits at the time all wait bits were set (before clearing), or an error.
     * @retval timeout The wait condition was not satisfied before the deadline.
     */
    template<typename Clock, typename Duration>
    [[nodiscard]] result<flags<E>>
    try_sync_until(flags<E> set_bits, flags<E> wait_bits, const std::chrono::time_point<Clock, Duration>& deadline) {
        auto remaining = deadline - Clock::now();
        if (remaining <= decltype(remaining)::zero()) {
            return _try_sync(set_bits, wait_bits, 0);
        }
        return _try_sync(set_bits, wait_bits, chrono::ticks(remaining));
    }

    // =========================================================================
    // ISR operations
    // =========================================================================

    /**
     * @headerfile <idfxx/event_group>
     * @brief Result of setting event bits from ISR context.
     */
    struct isr_set_result {
        bool success; ///< true if the set was posted to the timer daemon task successfully.
        bool yield;   ///< true if a context switch should be requested.
    };

    /**
     * @brief Sets event bits from ISR context.
     *
     * Setting bits from an ISR is deferred to the timer daemon task via
     * `xEventGroupSetBitsFromISR`. The operation may fail if the timer
     * command queue is full.
     *
     * @param bits The bits to set.
     * @return Result containing success status and whether a context switch
     *         should be requested.
     *
     * @note Pass the yield field to idfxx::yield_from_isr() to perform
     *       the context switch if needed.
     *
     * @code
     * void IRAM_ATTR my_isr() {
     *     auto [success, yield] = eg->set_from_isr(my_event::data_ready);
     *     idfxx::yield_from_isr(yield);
     * }
     * @endcode
     */
    [[nodiscard]] isr_set_result IRAM_ATTR set_from_isr(flags<E> bits) noexcept {
        BaseType_t woken = pdFALSE;
        BaseType_t ret = xEventGroupSetBitsFromISR(_handle, bits.value(), &woken);
        return {ret == pdPASS, woken == pdTRUE};
    }

    /**
     * @brief Clears event bits from ISR context.
     *
     * Clearing bits from an ISR is deferred via `xEventGroupClearBitsFromISR`.
     * The returned value is the bits *before* the deferred clear takes effect.
     *
     * @param bits The bits to clear.
     * @return The event group bits before the clear was requested.
     *
     * @note The clear is deferred to the timer daemon task. The bits may not
     *       be cleared immediately.
     */
    [[nodiscard]] flags<E> IRAM_ATTR clear_from_isr(flags<E> bits) noexcept {
        return flags<E>::from_raw(
            static_cast<std::underlying_type_t<E>>(xEventGroupClearBitsFromISR(_handle, bits.value()))
        );
    }

    /**
     * @brief Returns the current event bits from ISR context.
     *
     * @return The current event group bits.
     */
    [[nodiscard]] flags<E> IRAM_ATTR get_from_isr() const noexcept {
        return flags<E>::from_raw(static_cast<std::underlying_type_t<E>>(xEventGroupGetBitsFromISR(_handle)));
    }

    // =========================================================================
    // Handle access
    // =========================================================================

    /**
     * @brief Returns the underlying FreeRTOS event group handle.
     *
     * Provides access to the raw handle for interoperability with ESP-IDF
     * APIs that require an EventGroupHandle_t.
     *
     * @return The EventGroupHandle_t.
     */
    [[nodiscard]] EventGroupHandle_t idf_handle() const noexcept { return _handle; }

private:
    [[nodiscard]] result<flags<E>> _try_wait(flags<E> bits, wait_mode mode, bool clear_on_exit, TickType_t ticks) {
        EventBits_t result_bits = xEventGroupWaitBits(
            _handle, bits.value(), clear_on_exit ? pdTRUE : pdFALSE, mode == wait_mode::all ? pdTRUE : pdFALSE, ticks
        );
        auto result_flags = flags<E>::from_raw(static_cast<std::underlying_type_t<E>>(result_bits));
        bool satisfied = (mode == wait_mode::all) ? result_flags.contains(bits) : result_flags.contains_any(bits);
        if (!satisfied) {
            return error(errc::timeout);
        }
        return result_flags;
    }

    [[nodiscard]] result<flags<E>> _try_sync(flags<E> set_bits, flags<E> wait_bits, TickType_t ticks) {
        EventBits_t result_bits = xEventGroupSync(_handle, set_bits.value(), wait_bits.value(), ticks);
        auto result_flags = flags<E>::from_raw(static_cast<std::underlying_type_t<E>>(result_bits));
        if (!result_flags.contains(wait_bits)) {
            return error(errc::timeout);
        }
        return result_flags;
    }

    EventGroupHandle_t _handle;
};

/** @} */ // end of idfxx_event_group

} // namespace idfxx
