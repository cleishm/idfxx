// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/intr_types>
 * @file intr_types.hpp
 * @brief Interrupt type definitions.
 *
 * @ingroup idfxx_hw_support
 */

#include <esp_intr_types.h>

namespace idfxx {

/**
 * @brief Interrupt CPU core affinity.
 *
 * This type specify the CPU core that the peripheral interrupt is connected to.
 */
enum class intr_cpu_affinity_t {
    automatic = ESP_INTR_CPU_AFFINITY_AUTO, ///< Install the peripheral interrupt to ANY CPU core, decided by on which
                                            ///< CPU the interrupt allocator is running
    cpu_0 = ESP_INTR_CPU_AFFINITY_0,        ///< Install the peripheral interrupt to CPU core 0
    cpu_1 = ESP_INTR_CPU_AFFINITY_1,        ///< Install the peripheral interrupt to CPU core 1
};

/** @brief Convert esp_intr_cpu_affinity_t to CPU core ID */
[[nodiscard]] constexpr int intr_cpu_affinity_to_core_id(intr_cpu_affinity_t cpu_affinity) {
    return ESP_INTR_CPU_AFFINITY_TO_CORE_ID(static_cast<esp_intr_cpu_affinity_t>(cpu_affinity));
}

} // namespace idfxx
