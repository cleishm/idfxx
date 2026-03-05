// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/system>
 * @file system.hpp
 * @brief System information and control.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_system System
 * @ingroup idfxx_core
 * @brief Reset reason, restart, shutdown handlers, and heap information.
 * @{
 */

#include <idfxx/error>

#include <cstddef>
#include <esp_system.h>
#include <string>

namespace idfxx {

/**
 * @headerfile <idfxx/system>
 * @brief Reason for the most recent chip reset.
 *
 * @code
 * auto reason = idfxx::last_reset_reason();
 * if (reason == idfxx::reset_reason::panic) {
 *     // Handle crash recovery
 * }
 * @endcode
 */
enum class reset_reason : int {
    // clang-format off
    unknown            = 0,  ///< Reset reason could not be determined.
    power_on           = 1,  ///< Power-on reset.
    external           = 2,  ///< Reset by external pin (not applicable for ESP32).
    software           = 3,  ///< Software reset via esp_restart.
    panic              = 4,  ///< Software reset due to exception/panic.
    interrupt_watchdog = 5,  ///< Reset due to interrupt watchdog.
    task_watchdog      = 6,  ///< Reset due to task watchdog.
    watchdog           = 7,  ///< Reset due to other watchdog.
    deep_sleep         = 8,  ///< Reset after exiting deep sleep mode.
    brownout           = 9,  ///< Brownout reset (software or hardware).
    sdio               = 10, ///< Reset over SDIO.
    usb                = 11, ///< Reset by USB peripheral.
    jtag               = 12, ///< Reset by JTAG.
    efuse              = 13, ///< Reset due to efuse error.
    power_glitch       = 14, ///< Reset due to power glitch detected.
    cpu_lockup         = 15, ///< Reset due to CPU lock up.
    // clang-format on
};

/**
 * @brief Returns a string representation of a reset reason.
 *
 * @param r The reset reason to convert.
 * @return A descriptive string, or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(reset_reason r);

/**
 * @brief Returns the reason for the most recent chip reset.
 *
 * @return The reset reason.
 */
[[nodiscard]] inline reset_reason last_reset_reason() noexcept {
    return static_cast<reset_reason>(esp_reset_reason());
}

/**
 * @brief Restart the chip immediately.
 *
 * Performs a software reset. This function does not return. Registered shutdown
 * handlers are called before the restart occurs.
 */
[[noreturn]] inline void restart() {
    esp_restart();
}

// =============================================================================
// Shutdown handlers
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Registers a function to be called during chip shutdown/restart.
 *
 * Shutdown handlers are called in reverse order of registration when
 * esp_restart() is invoked.
 *
 * @param handler Function pointer to the shutdown handler.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void register_shutdown_handler(void (*handler)());

/**
 * @brief Unregisters a previously registered shutdown handler.
 *
 * @param handler Function pointer to the shutdown handler to remove.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on failure.
 */
void unregister_shutdown_handler(void (*handler)());
#endif

/**
 * @brief Registers a function to be called during chip shutdown/restart.
 *
 * Shutdown handlers are called in reverse order of registration when
 * esp_restart() is invoked.
 *
 * @param handler Function pointer to the shutdown handler.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state if the handler was already registered.
 */
[[nodiscard]] result<void> try_register_shutdown_handler(void (*handler)());

/**
 * @brief Unregisters a previously registered shutdown handler.
 *
 * @param handler Function pointer to the shutdown handler to remove.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state if the handler was not registered.
 */
[[nodiscard]] result<void> try_unregister_shutdown_handler(void (*handler)());

// =============================================================================
// Heap information
// =============================================================================

/**
 * @brief Returns the current free heap size in bytes.
 *
 * @return The number of bytes available in the heap.
 */
[[nodiscard]] inline std::size_t free_heap_size() noexcept {
    return esp_get_free_heap_size();
}

/**
 * @brief Returns the current free internal heap size in bytes.
 *
 * Internal memory is directly accessible by the CPU and is typically used
 * for performance-critical allocations.
 *
 * @return The number of bytes available in internal heap.
 */
[[nodiscard]] inline std::size_t free_internal_heap_size() noexcept {
    return esp_get_free_internal_heap_size();
}

/**
 * @brief Returns the minimum free heap size recorded since boot.
 *
 * This is the "high water mark" — the lowest free heap level reached
 * at any point during execution. Useful for diagnosing memory pressure.
 *
 * @return The minimum number of free bytes recorded since boot.
 */
[[nodiscard]] inline std::size_t minimum_free_heap_size() noexcept {
    return esp_get_minimum_free_heap_size();
}

/** @} */ // end of idfxx_core_system
/** @} */ // end of idfxx_core

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::reset_reason> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::reset_reason r, FormatContext& ctx) const {
        auto s = to_string(r);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
