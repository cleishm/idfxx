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
#include <string>

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

/**
 * @brief Returns a string representation of a CPU core identifier.
 *
 * @param c The core identifier to convert.
 * @return "CORE_0", "CORE_1" (on multi-core chips), or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(core_id c) {
    switch (c) {
    case core_id::core_0:
        return "CORE_0";
#if SOC_CPU_CORES_NUM > 1
    case core_id::core_1:
        return "CORE_1";
#endif
    default:
        return "unknown(" + std::to_string(static_cast<unsigned int>(c)) + ")";
    }
}

/** @} */ // end of idfxx_cpu

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::core_id> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::core_id c, FormatContext& ctx) const {
        auto s = to_string(c);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
