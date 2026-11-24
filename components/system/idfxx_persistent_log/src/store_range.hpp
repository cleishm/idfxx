// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Presents a run of another store's sectors as a separate store, so that the log and the fault
// slots can share one store.

#include "sector_store.hpp"

#include <idfxx/byte_view>
#include <idfxx/error>

#include <cstddef>

namespace idfxx::persistent_log {

/// Sectors [first, first + count) of a base store. The range has the base's sector size and write
/// size, and its offsets count from the start of sector `first`. Every access goes through the
/// base's public methods, which check the access against the base's bounds. The base must outlive
/// the range.
class store_range final : public sector_store {
public:
    /// A range of @p count of @p base's sectors, starting at sector @p first.
    /// @p count is at least 1, and `first + count` is at most `base.sector_count()`.
    store_range(sector_store& base, size_t first, size_t count) noexcept
        : sector_store(count * base.sector_size(), base.sector_size(), base.write_size())
        , _base(&base)
        , _first(first) {}

private:
    [[nodiscard]] size_t _at(size_t offset) const { return _first * sector_size() + offset; }

    [[nodiscard]] idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        return _base->try_read(_at(offset), dst);
    }

    [[nodiscard]] idfxx::result<void>
    do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        return _base->try_read_raw(_at(offset), dst);
    }

    [[nodiscard]] idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        return _base->try_write(_at(offset), src);
    }

    [[nodiscard]] idfxx::result<void> do_erase_sector(size_t index) override {
        return _base->try_erase_sector(_first + index);
    }

    sector_store* _base;
    size_t _first;
};

} // namespace idfxx::persistent_log
