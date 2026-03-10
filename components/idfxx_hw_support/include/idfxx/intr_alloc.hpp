// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/intr_alloc>
 * @file intr_alloc.hpp
 * @brief Interrupt allocation levels and flags.
 *
 * @defgroup idfxx_hw_support Hardware Support Component
 * @brief Hardware interrupt allocation and management for ESP32.
 *
 * Provides type-safe interrupt allocation levels and behavioral flags.
 *
 * Depends on @ref idfxx_core for flags support.
 * @{
 */

#include <idfxx/flags>

namespace idfxx {

/**
 * @headerfile <idfxx/intr_alloc>
 * @brief Hardware interrupt priority levels.
 *
 * Type-safe interrupt priority levels using bit-flag encoding for use with
 * `intr_levels`. Priority levels 1-3 (low/medium) can use C / C++
 * handlers. Levels 4-6 and NMI require assembly handlers and must pass
 * NULL as the handler function.
 *
 * Bit values match the ESP_INTR_FLAG_LEVEL* constants, occupying bits 1-7.
 * These do not overlap with `intr_flag` values (bits 8-11), so they can
 * be safely combined via `to_underlying(levels) | to_underlying(flags)`.
 */
enum class intr_level : int {
    level_1 = 1u << 1, ///< Level 1 interrupt (lowest priority)
    level_2 = 1u << 2, ///< Level 2 interrupt
    level_3 = 1u << 3, ///< Level 3 interrupt
    level_4 = 1u << 4, ///< Level 4 interrupt
    level_5 = 1u << 5, ///< Level 5 interrupt
    level_6 = 1u << 6, ///< Level 6 interrupt
    nmi = 1u << 7,     ///< Level 7 / NMI (highest priority)
};

} // namespace idfxx

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::intr_level> = true;

namespace idfxx {

/// A set of interrupt priority levels.
using intr_levels = flags<intr_level>;

/// Low and medium priority levels (1-3). These can be handled in C / C++.
inline constexpr intr_levels intr_level_lowmed = intr_level::level_1 | intr_level::level_2 | intr_level::level_3;

/// High priority levels (4-6 and NMI). These require assembly handlers.
inline constexpr intr_levels intr_level_high =
    intr_level::level_4 | intr_level::level_5 | intr_level::level_6 | intr_level::nmi;

/// All interrupt levels.
inline constexpr intr_levels intr_level_all = intr_level_lowmed | intr_level_high;

/**
 * @headerfile <idfxx/intr_alloc>
 * @brief Interrupt behavioral flags.
 *
 * Type-safe interrupt behavioral flags. These control sharing behavior
 * and handler requirements, but not priority levels.
 *
 * Bit values occupy bits 8-11, which do not overlap with `intr_level`
 * values (bits 1-7), so they can be safely combined via
 * `to_underlying(levels) | to_underlying(flags)`.
 */
enum class intr_flag : int {
    none = 0,            ///< No flags / default
    shared = 1u << 8,    ///< Interrupt can be shared between ISRs
    edge = 1u << 9,      ///< Edge-triggered interrupt
    iram = 1u << 10,     ///< ISR can be called if cache is disabled
    disabled = 1u << 11, ///< Return from ISR with interrupts disabled
};

} // namespace idfxx

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::intr_flag> = true;

/** @} */
