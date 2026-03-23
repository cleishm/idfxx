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

/**
 * @headerfile <idfxx/cpu>
 * @brief Type-safe wrapper for FreeRTOS task priority values.
 *
 * Wraps an unsigned integer priority level with implicit conversion from
 * unsigned int, so designated initializers like `.priority = 5` work
 * without change.
 *
 * @code
 * // Direct construction
 * task_priority p{10};
 *
 * // Implicit conversion from unsigned int
 * task_priority p = 5;
 *
 * // Access raw value for FreeRTOS APIs
 * vTaskPrioritySet(handle, p.value());
 * @endcode
 */
class task_priority {
public:
    /** @brief Default constructor. Initializes to priority 0 (idle). */
    constexpr task_priority() noexcept
        : _value(0) {}

    /**
     * @brief Constructs from an unsigned integer value.
     *
     * Non-explicit to allow implicit conversion in designated initializers.
     *
     * @param value The priority level.
     */
    constexpr task_priority(unsigned int value) noexcept
        : _value(value) {}

    /**
     * @brief Returns the raw priority value.
     * @return The priority as an unsigned integer.
     */
    [[nodiscard]] constexpr unsigned int value() const noexcept { return _value; }

    /** @brief Default three-way comparison. */
    [[nodiscard]] constexpr auto operator<=>(const task_priority&) const noexcept = default;

private:
    unsigned int _value;
};

/**
 * @brief Returns a string representation of a task priority.
 *
 * @param p The task priority to convert.
 * @return The priority value as a decimal string.
 */
[[nodiscard]] inline std::string to_string(task_priority p) {
    return std::to_string(p.value());
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

template<>
struct formatter<idfxx::task_priority> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::task_priority p, FormatContext& ctx) const {
        auto s = to_string(p);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
