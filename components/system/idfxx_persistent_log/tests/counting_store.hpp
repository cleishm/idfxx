// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// A store that counts the bytes read from it, so that a test can put a bound on what a call reads.

#pragma once

#include "../src/sector_store.hpp"

#include <idfxx/byte_view>
#include <idfxx/error>

#include <cstddef>
#include <memory>
#include <utility>

namespace idfxx::persistent_log::testing {

// A store in RAM that counts the bytes that it reads, raw or not.
class counting_store final : public sector_store {
public:
    counting_store(size_t sectors, size_t sector_size)
        : counting_store(std::make_unique<ram_sector_store>(sectors, sector_size)) {}

    // The number of bytes read since the store was made or since the last call to reset_count().
    [[nodiscard]] size_t bytes_read() const { return _read; }

    void reset_count() { _read = 0; }

private:
    explicit counting_store(std::unique_ptr<ram_sector_store> ram)
        : sector_store(ram->size(), ram->sector_size(), ram->write_size())
        , _ram(std::move(ram)) {}

    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        _read += dst.size();
        return _ram->try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        _read += dst.size();
        return _ram->try_read_raw(offset, dst);
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        return _ram->try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        return _ram->try_erase_sector(index);
    }

    std::unique_ptr<ram_sector_store> _ram;
    mutable size_t _read = 0;
};

} // namespace idfxx::persistent_log::testing
