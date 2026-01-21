// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/intr_alloc>
 * @file intr_alloc.hpp
 * @brief Interrupt allocation flags.
 *
 * @defgroup idfxx_hw_support Hardware Support Component
 * @brief Hardware interrupt allocation and management for ESP32.
 *
 * Provides type-safe interrupt allocation flags and CPU affinity types.
 *
 * Depends on @ref idfxx_core for flags support.
 * @{
 */

#include <idfxx/flags>

#include <esp_intr_alloc.h>

namespace idfxx {

/**
 * @brief Interrupt allocation flags.
 *
 * Type-safe interrupt allocation flags. These flags control
 * interrupt priority levels, sharing behavior, and handler requirements.
 *
 * Priority levels 1-3 (lowmed) can use C / C++ handlers. Levels 4-6 and NMI
 * require assembly handlers and must pass NULL as the handler function.
 */
enum class intr_flag : int {
    none = 0,                 ///< No flags / default
    level1 = 1u << 1,         ///< Accept Level 1 interrupt (lowest priority)
    level2 = 1u << 2,         ///< Accept Level 2 interrupt
    level3 = 1u << 3,         ///< Accept Level 3 interrupt
    level4 = 1u << 4,         ///< Accept Level 4 interrupt
    level5 = 1u << 5,         ///< Accept Level 5 interrupt
    level6 = 1u << 6,         ///< Accept Level 6 interrupt
    nmi = 1u << 7,            ///< Accept Level 7 / NMI (highest priority)
    shared = 1u << 8,         ///< Interrupt can be shared between ISRs
    edge = 1u << 9,           ///< Edge-triggered interrupt
    iram = 1u << 10,          ///< ISR can be called if cache is disabled
    intr_disabled = 1u << 11, ///< Return from ISR with interrupts disabled
};

} // namespace idfxx

template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::intr_flag> = true;

namespace idfxx {

/// Low and medium priority levels (1-3). These can be handled in C / C++.
inline constexpr flags<intr_flag> intr_flag_lowmed = intr_flag::level1 | intr_flag::level2 | intr_flag::level3;

/// High priority levels (4-6 and NMI). These require assembly handlers.
inline constexpr flags<intr_flag> intr_flag_high =
    intr_flag::level4 | intr_flag::level5 | intr_flag::level6 | intr_flag::nmi;

/// Mask of all interrupt level flags.
inline constexpr flags<intr_flag> intr_flag_levelmask = intr_flag_lowmed | intr_flag_high;

} // namespace idfxx

/** @} */
