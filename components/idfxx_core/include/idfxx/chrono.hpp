// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/chrono>
 * @file chrono.hpp
 * @brief IDFXX chrono utilities.
 *
 * @addtogroup idfxx_core
 * @{
 * @defgroup idfxx_core_chrono Chrono Utilities
 * @brief FreeRTOS tick conversions using std::chrono.
 *
 * Provides utilities for converting `std::chrono::duration` to FreeRTOS ticks.
 * @{
 */

#include <chrono>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief ESP-IDF C++ chrono utilities.
 */
namespace idfxx::chrono {

/**
 * @brief Converts a std::chrono duration to TickType_t ticks.
 */
template<class Rep, class Period>
[[nodiscard]] constexpr TickType_t ticks(const std::chrono::duration<Rep, Period>& d) {
    return pdMS_TO_TICKS(std::chrono::ceil<std::chrono::milliseconds>(d).count());
}

/**
 * @headerfile <idfxx/chrono>
 * @brief Steady clock based on FreeRTOS tick count.
 *
 * Provides a std::chrono-compatible clock with tick-based precision.
 * The tick rate is determined by `configTICK_RATE_HZ`.
 */
struct tick_clock {
    using rep = TickType_t;
    using period = std::ratio<1, configTICK_RATE_HZ>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<tick_clock>;
    static constexpr bool is_steady = true;

    /**
     * @brief Returns the current tick count as a time_point.
     * @return Current time as a time_point.
     */
    [[nodiscard]] static time_point now() noexcept { return time_point{duration{xTaskGetTickCount()}}; }

    /**
     * @brief Returns the current tick count as a time_point from ISR context.
     * @return Current time as a time_point.
     */
    [[nodiscard]] static time_point now_from_isr() noexcept { return time_point{duration{xTaskGetTickCountFromISR()}}; }
};

/** @} */ // end of idfxx_core_chrono
/** @} */ // end of idfxx_core

} // namespace idfxx::chrono
