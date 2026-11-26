// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "capture.hpp"

namespace idfxx::persistent_log::internal {

level_filter::level_filter(
    idfxx::log::level level,
    std::vector<std::pair<std::string, idfxx::log::level>> tag_levels
)
    : _level(level)
    , _tag_levels(std::move(tag_levels)) {}

idfxx::log::level level_filter::threshold(std::string_view tag) const noexcept {
    // The last entry for a tag is used, because later settings override earlier ones.
    for (auto it = _tag_levels.rbegin(); it != _tag_levels.rend(); ++it) {
        if (it->first == tag) {
            return it->second;
        }
    }
    return _level;
}

} // namespace idfxx::persistent_log::internal
