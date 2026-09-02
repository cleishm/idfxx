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
 * @brief std::chrono clocks and FreeRTOS tick conversions.
 *
 * Provides utilities for converting `std::chrono::duration` to FreeRTOS ticks, and
 * std::chrono-compatible clocks over the FreeRTOS tick count and the RTC timer.
 * @{
 */

#include <chrono>
#include <cstdint>
#include <esp_rtc_time.h>
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

/**
 * @headerfile <idfxx/chrono>
 * @brief Steady clock based on the RTC timer.
 *
 * Provides a std::chrono-compatible clock with microsecond resolution that counts from
 * the last power-on reset. The RTC timer keeps running through light sleep, deep sleep,
 * and every kind of reset other than power-on, so time points taken before a deep sleep
 * or a software reset remain comparable with those taken afterwards. By contrast,
 * `std::chrono::steady_clock` restarts from zero at every boot (including wake-up from
 * deep sleep), and `std::chrono::system_clock` jumps whenever the wall-clock time is set.
 *
 * The clock never runs backwards. Its accuracy follows the RTC slow clock source selected
 * in the project configuration: the default internal RC oscillator drifts with
 * temperature, while an external 32 kHz crystal keeps time well. Recalibrating the slow
 * clock changes only the rate at which the clock advances from that point on.
 *
 * @code
 * using namespace std::chrono_literals;
 * using idfxx::chrono::rtc_clock;
 *
 * // Time since power-on, including time spent in deep sleep
 * auto uptime = rtc_clock::now().time_since_epoch();
 *
 * // Rate-limit an action across deep-sleep cycles: the time point lives in RTC
 * // memory, so it survives the wake-up reset
 * RTC_DATA_ATTR static rtc_clock::time_point last_report;
 * if (rtc_clock::now() - last_report >= 1h) {
 *     send_report();
 *     last_report = rtc_clock::now();
 * }
 * @endcode
 */
struct rtc_clock {
    using duration = std::chrono::microseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<rtc_clock>;
    static constexpr bool is_steady = true;

    /**
     * @brief Returns the time elapsed since the last power-on reset as a time_point.
     *
     * Reading the RTC timer can take up to one RTC slow clock cycle (roughly 7 µs with the
     * internal RC oscillator, 30 µs with a 32 kHz crystal). Safe to call from an ISR.
     *
     * @return Current time as a time_point.
     */
    [[nodiscard]] static time_point now() noexcept {
        return time_point{duration{static_cast<rep>(esp_rtc_get_time_us())}};
    }
};

/** @} */ // end of idfxx_core_chrono
/** @} */ // end of idfxx_core

} // namespace idfxx::chrono
