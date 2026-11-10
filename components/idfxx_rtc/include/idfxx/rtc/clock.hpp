// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/rtc/clock>
 * @file clock.hpp
 * @brief Abstract external real-time clock interface.
 *
 * @defgroup idfxx_rtc RTC Component
 * @brief Abstract interface for external real-time-clock chips.
 *
 * Provides a chip-agnostic abstract base class (`idfxx::rtc::clock`) that drivers
 * for battery-backed real-time-clock chips, such as `idfxx::rtc::pcf8563`,
 * implement. Application code binds to this interface to read and set the
 * calendar time without depending on a specific chip.
 *
 * `idfxx::rtc` covers external real-time-clock chips only. It is unrelated to
 * `idfxx::chrono::rtc_clock`, which reads the ESP32's internal RTC timer (time
 * since power-on, kept through deep sleep).
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error>

#include <chrono>
#include <optional>

/**
 * @headerfile <idfxx/rtc/clock>
 * @brief External real-time-clock interface and drivers.
 */
namespace idfxx::rtc {

/**
 * @headerfile <idfxx/rtc/clock>
 * @brief Abstract base class for external real-time-clock chips.
 *
 * A real-time clock keeps calendar time in UTC, usually from a backup battery,
 * while the rest of the system is unpowered. Read it at boot to restore the
 * system time with `idfxx::chrono::set_system_time()`:
 *
 * @code
 * void restore_system_time(idfxx::rtc::clock& rtc) {
 *     if (auto t = rtc.now()) {
 *         idfxx::chrono::set_system_time(*t);
 *     }
 * }
 * @endcode
 *
 * **Validity.** A clock's time is valid while the chip is keeping reliable
 * time: it has not lost track (for example after a supply or oscillator
 * failure), it is not stopped, and it holds a time the chip can represent.
 * A time that is not valid is an expected state, such as on first boot or
 * after a battery change, not an error: now() returns `std::nullopt` while
 * the time is not valid, and set_time() makes it valid again (set_time() also
 * restarts a stopped clock).
 *
 * **Errors.** An error from now() or set_time(), such as
 * `errc::invalid_response` or `errc::timeout`, means a transfer with the chip
 * failed: retry. set_time() also fails with `errc::invalid_arg` for a time the
 * chip can't hold.
 *
 * The public interface is non-virtual; concrete drivers customize behavior by
 * overriding the protected `do_*` hooks, mirroring the standard library's
 * non-virtual-interface pattern (cf. `std::pmr::memory_resource`). The range of
 * times a clock can hold is chip-specific, and each driver documents and checks
 * its own.
 *
 * The interface is not copyable; drivers are move-only.
 */
class clock {
public:
    virtual ~clock() = default;

    clock(const clock&) = delete;
    clock& operator=(const clock&) = delete;

    // =========================================================================
    // Time
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Reads the current time from the clock.
     *
     * @return The current time (UTC), to the second, or `std::nullopt` while the
     *         clock's time is not valid: the chip has lost track of the time (for
     *         example after a loss of backup power or an oscillator failure) and
     *         it hasn't been set since, the clock is stopped, or it holds a time
     *         the chip can't represent. set_time() makes the time valid again.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error if a transfer with the chip fails.
     *
     * @code
     * if (auto t = rtc.now()) {
     *     idfxx::chrono::set_system_time(*t);
     * }
     * @endcode
     */
    [[nodiscard]] std::optional<std::chrono::sys_seconds> now() { return unwrap(try_now()); }

    /**
     * @brief Sets the clock's time.
     *
     * Writes the time and leaves the clock running and its time valid: it
     * clears the chip's record that it lost track of time, in the same
     * operation that writes the time, and restarts a stopped clock (on chips
     * that can be stopped). So now() returns the time afterwards.
     *
     * @param t The new time (UTC), to the second.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error with `errc::invalid_arg` if @p t is outside the range the chip
     *         can hold (nothing is written), or with another code if a transfer with the chip
     *         fails.
     *
     * @code
     * rtc.set_time(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
     * @endcode
     */
    void set_time(std::chrono::sys_seconds t) { unwrap(try_set_time(t)); }
#endif

    /**
     * @brief Reads the current time from the clock.
     *
     * @return The current time (UTC), to the second, or `std::nullopt` while the
     *         clock's time is not valid: the chip has lost track of the time (for
     *         example after a loss of backup power or an oscillator failure) and
     *         it hasn't been set since, the clock is stopped, or it holds a time
     *         the chip can't represent. try_set_time() makes the time valid
     *         again. An error if a transfer with the chip fails.
     */
    [[nodiscard]] result<std::optional<std::chrono::sys_seconds>> try_now() { return do_now(); }

    /**
     * @brief Sets the clock's time.
     *
     * Writes the time and leaves the clock running and its time valid: it
     * clears the chip's record that it lost track of time, in the same
     * operation that writes the time, and restarts a stopped clock (on chips
     * that can be stopped). So try_now() returns the time afterwards.
     *
     * @param t The new time (UTC), to the second.
     * @return Success, or an error. Any other error means a transfer with the
     *         chip failed.
     * @retval invalid_arg @p t is outside the range the chip can hold; nothing was written.
     */
    [[nodiscard]] result<void> try_set_time(std::chrono::sys_seconds t) { return do_set_time(t); }

protected:
    clock() = default;
    clock(clock&&) noexcept = default;
    clock& operator=(clock&&) noexcept = default;

    // =========================================================================
    // Customization hooks
    //
    // Concrete drivers override these (typically privately). Each hook
    // implements the correspondingly named public method. The public wrappers
    // perform no validation: the range of times a chip can hold is
    // chip-specific, so drivers check it themselves.
    // =========================================================================

    /// Hook for @ref try_now. Must return `std::nullopt` exactly when the time
    /// is not valid (the chip has lost track of the time, is stopped, or holds
    /// a time it can't represent), and report errors only for transfer
    /// failures.
    [[nodiscard]] virtual result<std::optional<std::chrono::sys_seconds>> do_now() = 0;
    /// Hook for @ref try_set_time. Must report `errc::invalid_arg`, writing
    /// nothing, for a time the chip cannot hold, and must clear the chip's
    /// lost-time indication in the same operation that writes the time. It
    /// must also restart a stopped clock, so that after it succeeds, the time
    /// is valid: do_now() no longer returns `std::nullopt`.
    [[nodiscard]] virtual result<void> do_set_time(std::chrono::sys_seconds t) = 0;
};

} // namespace idfxx::rtc

/** @} */ // end of idfxx_rtc
