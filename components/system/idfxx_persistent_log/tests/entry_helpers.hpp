// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Helpers that the tests use to read entries and to compare lists of entries.

#pragma once

#include "idfxx/persistent_log"
#include "unity.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace idfxx::persistent_log::testing {

// The stored text of each entry that `r` returns.
inline std::vector<std::string> stored_entries(entry_reader& r) {
    std::vector<std::string> out;
    entry e;
    while (true) {
        auto more = r.try_next(e);
        TEST_ASSERT_TRUE(more.has_value());
        if (!*more) {
            return out;
        }
        out.emplace_back(idfxx::persistent_log::detail::entry_access::stored(e));
    }
}

// The stored text of each entry that read_newest(count) returns.
inline std::vector<std::string> read_newest(size_t count) {
    auto r = try_read_newest(count);
    TEST_ASSERT_TRUE(r.has_value());
    return stored_entries(*r);
}

// The last `count` of `all`, or all of them.
inline std::vector<std::string> last_of(const std::vector<std::string>& all, size_t count) {
    return {all.end() - static_cast<ptrdiff_t>(std::min(count, all.size())), all.end()};
}

// The stored text of each entry in `text`, as next_entry() splits it.
inline std::vector<std::string> split_entries(std::string_view text) {
    std::vector<std::string> out;
    entry e;
    while (next_entry(text, e)) {
        out.emplace_back(idfxx::persistent_log::detail::entry_access::stored(e));
    }
    return out;
}

} // namespace idfxx::persistent_log::testing
