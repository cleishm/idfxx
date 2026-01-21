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

/**
 * @brief ESP-IDF C++ chrono utilities.
 */
namespace idfxx::chrono {

/**
 * @brief Converts a std::chrono duration to TickType_t ticks.
 */
template<class Rep, class Period>
[[nodiscard]] constexpr TickType_t to_ticks(const std::chrono::duration<Rep, Period>& d) {
    return pdMS_TO_TICKS(std::chrono::ceil<std::chrono::milliseconds>(d).count());
}

/** @} */ // end of idfxx_core_chrono
/** @} */ // end of idfxx_core

} // namespace idfxx::chrono
