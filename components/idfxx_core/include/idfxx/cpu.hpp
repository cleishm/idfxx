// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/cpu>
 * @file cpu.hpp
 * @brief CPU-related types and utilities.
 *
 * @defgroup idfxx_cpu CPU Utilities
 * @brief CPU identification and core affinity types.
 * @{
 */

#include <soc/soc_caps.h>

namespace idfxx {

/**
 * @headerfile <idfxx/cpu>
 * @brief Identifies a specific CPU core.
 *
 * Use with `std::optional<core_id>` for core affinity settings,
 * where `std::nullopt` means "any core".
 *
 * @note Only cores available on the target chip are defined.
 *       On single-core chips, only `core_0` exists.
 *
 * @code
 * // Pin task to core 0
 * .core_affinity = core_id::core_0
 *
 * // Allow task to run on any core
 * .core_affinity = std::nullopt
 * @endcode
 */
enum class core_id : unsigned int {
    core_0 = 0,
#if SOC_CPU_CORES_NUM > 1
    core_1 = 1,
#endif
};

/** @} */ // end of idfxx_cpu

} // namespace idfxx
