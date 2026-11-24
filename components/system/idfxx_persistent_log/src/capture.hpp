// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// The level filter that decides which captured lines are kept. It uses none of the service's state,
// so the tests can drive it. It is built only with capture, because it uses idfxx::log's levels.

#include "sdkconfig.h"

#ifdef CONFIG_IDFXX_STD_FORMAT

#include <idfxx/log>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace idfxx::persistent_log::internal {

/// True when a line at level @p line passes the threshold @p threshold, by the same rule as
/// esp_log. A threshold of `none` passes no line.
[[nodiscard]] constexpr bool passes(idfxx::log::level line, idfxx::log::level threshold) noexcept {
    return line != idfxx::log::level::none && line <= threshold;
}

/// Decides which captured lines are kept. One threshold applies to every tag, except the tags that
/// have their own threshold.
class level_filter {
public:
    level_filter() = default;

    /**
     * @param level      The threshold for tags that don't have their own.
     * @param tag_levels The tags that have their own threshold. When a tag appears more than once,
     *                   the last entry is used.
     */
    level_filter(
        idfxx::log::level level,
        std::vector<std::pair<std::string, idfxx::log::level>> tag_levels
    );

    /// The threshold for @p tag.
    [[nodiscard]] idfxx::log::level threshold(std::string_view tag) const noexcept;

    /// True when a line at level @p line with tag @p tag is kept.
    [[nodiscard]] bool keeps(idfxx::log::level line, std::string_view tag) const noexcept {
        return passes(line, threshold(tag));
    }

private:
    idfxx::log::level _level = idfxx::log::level::none;
    std::vector<std::pair<std::string, idfxx::log::level>> _tag_levels;
};

} // namespace idfxx::persistent_log::internal

#endif // CONFIG_IDFXX_STD_FORMAT
