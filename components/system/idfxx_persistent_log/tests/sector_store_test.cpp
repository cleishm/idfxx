// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_persistent_log: the sector stores. The partition store's round trip uses the
// test app's phy_init partition, which nothing in the tests reads.

#include "../src/sector_store.hpp"
#include "unity.h"

#include <idfxx/partition>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <system_error>

using namespace idfxx::persistent_log;

namespace {

bool all_are(std::span<const std::byte> bytes, std::byte b) {
    return std::ranges::all_of(bytes, [b](std::byte x) { return x == b; });
}

bool none_are(std::span<const std::byte> bytes, std::byte b) {
    return std::ranges::none_of(bytes, [b](std::byte x) { return x == b; });
}

// Converts 16 characters of text to a 16-byte block.
std::array<std::byte, 16> block_of(const char (&text)[17]) {
    std::array<std::byte, 16> out;
    for (size_t i = 0; i < 16; ++i) {
        out[i] = std::byte(text[i]);
    }
    return out;
}

std::array<std::byte, 16> read_block(const sector_store& store, size_t offset) {
    std::array<std::byte, 16> out;
    TEST_ASSERT_TRUE(store.try_read(offset, out).has_value());
    return out;
}

std::array<std::byte, 16> read_raw_block(const sector_store& store, size_t offset) {
    std::array<std::byte, 16> out;
    TEST_ASSERT_TRUE(store.try_read_raw(offset, out).has_value());
    return out;
}

} // namespace

TEST_CASE("ram_sector_store starts erased, and writes only clear bits", "[persistent_log]") {
    ram_sector_store store(4, 64);
    TEST_ASSERT_EQUAL_size_t(256, store.size());
    TEST_ASSERT_EQUAL_size_t(64, store.sector_size());
    TEST_ASSERT_EQUAL_size_t(4, store.sector_count());
    TEST_ASSERT_TRUE(all_are(store.bytes(), std::byte{0xFF}));

    std::array<std::byte, 2> a = {std::byte{0xF0}, std::byte{0x3C}};
    TEST_ASSERT_TRUE(store.try_write(70, a).has_value());
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    std::array<std::byte, 2> b = {std::byte{0x0F}, std::byte{0xFF}};
    TEST_ASSERT_TRUE(store.try_write(70, b).has_value());
    TEST_ASSERT_EQUAL_size_t(2, store.overwrites());
    std::array<std::byte, 2> got;
    TEST_ASSERT_TRUE(store.try_read(70, got).has_value());
    TEST_ASSERT_EQUAL_HEX8(0x00, std::to_integer<int>(got[0]));
    TEST_ASSERT_EQUAL_HEX8(0x3C, std::to_integer<int>(got[1]));

    TEST_ASSERT_TRUE(store.try_erase_sector(1).has_value());
    TEST_ASSERT_TRUE(all_are(store.bytes(), std::byte{0xFF}));
    TEST_ASSERT_EQUAL_size_t(1, store.erases());
}

TEST_CASE("ram_sector_store reads raw as it reads, and writes at any byte", "[persistent_log]") {
    ram_sector_store store(4, 64);
    TEST_ASSERT_EQUAL_size_t(1, store.write_size());
    std::array<std::byte, 3> data = {std::byte{1}, std::byte{2}, std::byte{3}};
    TEST_ASSERT_TRUE(store.try_write(5, data).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(read_block(store, 0), read_raw_block(store, 0)));
    TEST_ASSERT_EQUAL_HEX8(2, std::to_integer<int>(read_raw_block(store, 0)[6]));
    TEST_ASSERT_EQUAL_HEX8(0xFF, std::to_integer<int>(read_raw_block(store, 0)[8]));
}

TEST_CASE("ram_sector_store refuses ranges outside the region", "[persistent_log]") {
    ram_sector_store store(4, 64);
    std::array<std::byte, 8> buf{};
    TEST_ASSERT_FALSE(store.try_read(250, buf).has_value());
    TEST_ASSERT_FALSE(store.try_write(250, buf).has_value());
    TEST_ASSERT_FALSE(store.try_read(SIZE_MAX - 2, buf).has_value());
    TEST_ASSERT_FALSE(store.try_erase_sector(4).has_value());
    TEST_ASSERT_FALSE(store.try_read_raw(250, buf).has_value());
    auto r = store.try_read_raw(SIZE_MAX - 2, buf);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(store.try_read(248, buf).has_value());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("sector_store's throwing calls throw what the try_ calls return", "[persistent_log]") {
    ram_sector_store store(4, 64);
    std::array<std::byte, 8> buf{};
    store.write(8, buf);
    store.read(8, buf);
    store.read_raw(8, buf);
    store.erase_sector(0);
    auto code_of = [](auto&& call) {
        try {
            call();
        } catch (const std::system_error& e) {
            return e.code();
        }
        return std::error_code();
    };
    TEST_ASSERT_TRUE(code_of([&] { store.read(250, buf); }) == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(code_of([&] { store.read_raw(250, buf); }) == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(code_of([&] { store.write(250, buf); }) == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(code_of([&] { store.erase_sector(4); }) == idfxx::errc::invalid_arg);
    store.cut_after(0);
    TEST_ASSERT_TRUE(code_of([&] { store.write(0, buf); }) == idfxx::errc::invalid_state);
}
#endif

// =============================================================================
// Encrypted RAM store
// =============================================================================

TEST_CASE("encrypted ram_sector_store writes only whole aligned blocks", "[persistent_log]") {
    ram_sector_store store(ram_sector_store::encrypted, 4, 64);
    TEST_ASSERT_EQUAL_size_t(16, store.write_size());
    std::array<std::byte, 32> buf{};
    for (auto [offset, length] : {std::pair<size_t, size_t>{1, 16}, {8, 16}, {0, 15}, {16, 17}}) {
        auto r = store.try_write(offset, std::span(buf).first(length));
        TEST_ASSERT_FALSE(r.has_value());
        TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    }
    TEST_ASSERT_TRUE(all_are(store.bytes(), std::byte{0xFF}));
    TEST_ASSERT_TRUE(store.try_write(16, buf).has_value());
    TEST_ASSERT_TRUE(store.try_write(240, std::span(buf).first(16)).has_value());
    // Reads can start at any byte.
    std::array<std::byte, 5> part;
    TEST_ASSERT_TRUE(store.try_read(19, part).has_value());
    TEST_ASSERT_TRUE(all_are(part, std::byte{0}));
}

TEST_CASE("encrypted ram_sector_store stores ciphertext, read back decrypted", "[persistent_log]") {
    ram_sector_store store(ram_sector_store::encrypted, 4, 64);
    // An erased block reads raw as 0xFF, and decrypts to noise that contains no 0xFF byte.
    TEST_ASSERT_TRUE(all_are(read_raw_block(store, 32), std::byte{0xFF}));
    TEST_ASSERT_TRUE(none_are(read_block(store, 32), std::byte{0xFF}));

    auto text = block_of("sixteen bytes!!\n");
    TEST_ASSERT_TRUE(store.try_write(32, text).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(text, read_block(store, 32)));
    auto raw = read_raw_block(store, 32);
    TEST_ASSERT_FALSE(std::ranges::equal(text, raw));
    TEST_ASSERT_TRUE(std::ranges::equal(raw, store.bytes().subspan(32, 16)));
    // The keystream depends on the address, so the same text is stored as different bytes at
    // another address.
    TEST_ASSERT_TRUE(store.try_write(48, text).has_value());
    TEST_ASSERT_FALSE(std::ranges::equal(raw, read_raw_block(store, 48)));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());

    // Writing the same text again stores the same bytes. Nothing changes, and no overwrite is
    // counted.
    TEST_ASSERT_TRUE(store.try_write(32, text).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(raw, read_raw_block(store, 32)));
    TEST_ASSERT_TRUE(std::ranges::equal(text, read_block(store, 32)));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());

    // Writing different text over the block ANDs the stored bytes, and the entire block then
    // reads back as noise.
    auto other = block_of("another sixteen!");
    TEST_ASSERT_TRUE(store.try_write(32, other).has_value());
    TEST_ASSERT_GREATER_THAN_size_t(0, store.overwrites());
    auto anded = read_raw_block(store, 32);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_TRUE((anded[i] & ~raw[i]) == std::byte{0});
    }
    auto noise = read_block(store, 32);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_TRUE(noise[i] != text[i]);
        TEST_ASSERT_TRUE(noise[i] != other[i]);
    }

    // Erasing the sector makes the block clean again.
    TEST_ASSERT_TRUE(store.try_erase_sector(0).has_value());
    TEST_ASSERT_TRUE(all_are(read_raw_block(store, 32), std::byte{0xFF}));
    TEST_ASSERT_TRUE(store.try_write(32, other).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(other, read_block(store, 32)));
}

TEST_CASE("encrypted ram_sector_store garbles a whole block a power cut tore", "[persistent_log]") {
    ram_sector_store store(ram_sector_store::encrypted, 4, 64);
    auto a = block_of("first block, ok\n");
    auto b = block_of("second, torn hlf");
    std::array<std::byte, 32> both;
    std::ranges::copy(a, both.begin());
    std::ranges::copy(b, both.begin() + 16);
    store.cut_after(16 + 7);
    TEST_ASSERT_FALSE(store.try_write(64, both).has_value());
    TEST_ASSERT_TRUE(store.cut());
    store.restore_power();
    // The first block was written completely. Of the second block, 7 bytes were written and 9 are
    // still erased, but none of its 16 bytes reads back as written or as erased.
    TEST_ASSERT_TRUE(std::ranges::equal(a, read_block(store, 64)));
    auto raw = read_raw_block(store, 80);
    TEST_ASSERT_TRUE(none_are(std::span(raw).first(7), std::byte{0xFF}));
    TEST_ASSERT_TRUE(all_are(std::span(raw).subspan(7), std::byte{0xFF}));
    ram_sector_store fresh(ram_sector_store::encrypted, 4, 64);
    auto erased_reads = read_block(fresh, 80);
    auto got = read_block(store, 80);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_TRUE(got[i] != (i < 7 ? b[i] : erased_reads[i]));
    }

    // A power cut before any byte is written leaves the block erased, not torn.
    store.cut_after(0);
    TEST_ASSERT_FALSE(store.try_write(96, a).has_value());
    store.restore_power();
    TEST_ASSERT_TRUE(all_are(read_raw_block(store, 96), std::byte{0xFF}));
    TEST_ASSERT_TRUE(store.try_write(96, a).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(a, read_block(store, 96)));

    // Writing the complete torn block again completes it, as programming the rest of its bits
    // would.
    TEST_ASSERT_TRUE(store.try_write(80, b).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(b, read_block(store, 80)));
    TEST_ASSERT_EQUAL_size_t(7, store.overwrites());

    // An erase that the power cut interrupts leaves the first half of its sector as it was. A
    // block in the half that it erases is clean.
    store.cut_after(7);
    TEST_ASSERT_FALSE(store.try_write(128 + 48, b).has_value());
    store.restore_power();
    TEST_ASSERT_FALSE(std::ranges::equal(b, read_block(store, 128 + 48)));
    TEST_ASSERT_TRUE(store.try_write(128, a).has_value());
    store.cut_after(0);
    TEST_ASSERT_FALSE(store.try_erase_sector(2).has_value());
    TEST_ASSERT_TRUE(store.cut_erasing());
    store.restore_power();
    TEST_ASSERT_TRUE(std::ranges::equal(a, read_block(store, 128)));
    TEST_ASSERT_TRUE(all_are(read_raw_block(store, 128 + 48), std::byte{0xFF}));
    TEST_ASSERT_TRUE(store.try_write(128 + 48, b).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(b, read_block(store, 128 + 48)));
}

TEST_CASE("ram_sector_store cuts the power after a budget of bytes", "[persistent_log]") {
    ram_sector_store store(4, 64);
    std::array<std::byte, 4> zeros{};
    store.cut_after(6);
    TEST_ASSERT_TRUE(store.try_write(0, zeros).has_value());
    TEST_ASSERT_FALSE(store.cut());
    TEST_ASSERT_FALSE(store.try_write(4, zeros).has_value());
    TEST_ASSERT_TRUE(store.cut());
    TEST_ASSERT_FALSE(store.cut_erasing());
    // Two of the second write's four bytes were written before the power cut.
    TEST_ASSERT_TRUE(all_are(store.bytes().first(6), std::byte{0}));
    TEST_ASSERT_TRUE(all_are(store.bytes().subspan(6), std::byte{0xFF}));
    std::array<std::byte, 1> one;
    TEST_ASSERT_FALSE(store.try_read(0, one).has_value());
    TEST_ASSERT_FALSE(store.try_erase_sector(0).has_value());

    store.restore_power();
    TEST_ASSERT_TRUE(store.try_read(0, one).has_value());
    TEST_ASSERT_TRUE(store.try_write(64, zeros).has_value());

    // An erase that the power cut interrupts erases only the second half of its sector.
    store.cut_after(0);
    TEST_ASSERT_FALSE(store.try_erase_sector(0).has_value());
    TEST_ASSERT_TRUE(store.cut_erasing());
    store.restore_power();
    TEST_ASSERT_TRUE(all_are(store.bytes().first(6), std::byte{0}));
    TEST_ASSERT_TRUE(all_are(store.bytes().subspan(32, 32), std::byte{0xFF}));
}

TEST_CASE("partition_sector_store keeps to its range of the partition", "[persistent_log]") {
    auto part = idfxx::partition::try_find("phy_init");
    TEST_ASSERT_TRUE(part.has_value());
    size_t unit = part->erase_size();
    TEST_ASSERT_EQUAL_size_t(4096, unit);

    TEST_ASSERT_FALSE(partition_sector_store::make(*part, 1, unit).has_value());
    TEST_ASSERT_FALSE(partition_sector_store::make(*part, 0, unit + 1).has_value());
    TEST_ASSERT_FALSE(partition_sector_store::make(*part, 0, 0).has_value());
    TEST_ASSERT_FALSE(partition_sector_store::make(*part, unit, part->size()).has_value());

    auto store = partition_sector_store::make(*part, 0, unit);
    TEST_ASSERT_TRUE(store.has_value());
    TEST_ASSERT_EQUAL_size_t(unit, store->size());
    TEST_ASSERT_EQUAL_size_t(unit, store->sector_size());
    std::array<std::byte, 8> buf{};
    TEST_ASSERT_FALSE(store->try_read(unit - 4, buf).has_value());
    TEST_ASSERT_FALSE(store->try_write(unit - 4, buf).has_value());
    TEST_ASSERT_FALSE(store->try_erase_sector(1).has_value());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE(
    "partition_sector_store's constructor throws for a range make() refuses",
    "[persistent_log]"
) {
    auto part = idfxx::partition::find("phy_init");
    size_t unit = part.erase_size();
    auto code_of = [&](size_t offset, size_t length) {
        try {
            partition_sector_store store(part, offset, length);
            TEST_ASSERT_EQUAL_size_t(length, store.size());
        } catch (const std::system_error& e) {
            return e.code();
        }
        return std::error_code();
    };
    TEST_ASSERT_TRUE(code_of(1, unit) == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(code_of(0, 0) == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(code_of(0, unit) == std::error_code());
}
#endif

TEST_CASE("partition_sector_store over a whole partition", "[persistent_log]") {
    auto part = idfxx::partition::try_find("phy_init");
    TEST_ASSERT_TRUE(part.has_value());
    auto store = partition_sector_store::make(*part);
    TEST_ASSERT_TRUE(store.has_value());
    TEST_ASSERT_EQUAL_size_t(part->size(), store->size());
    TEST_ASSERT_EQUAL_size_t(part->erase_size(), store->sector_size());
    TEST_ASSERT_EQUAL_size_t(part->size() / part->erase_size(), store->sector_count());
    std::array<std::byte, 8> buf{};
    TEST_ASSERT_TRUE(store->try_read(part->size() - buf.size(), buf).has_value());
    TEST_ASSERT_FALSE(store->try_read(part->size() - 4, buf).has_value());
    TEST_ASSERT_FALSE(store->try_erase_sector(store->sector_count()).has_value());

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    partition_sector_store thrown(*part);
    TEST_ASSERT_EQUAL_size_t(part->size(), thrown.size());
#endif
}

TEST_CASE("partition_sector_store erases, writes and reads flash", "[persistent_log]") {
    auto part = idfxx::partition::try_find("phy_init");
    TEST_ASSERT_TRUE(part.has_value());
    auto store = partition_sector_store::make(*part, 0, part->erase_size());
    TEST_ASSERT_TRUE(store.has_value());

    TEST_ASSERT_TRUE(store->try_erase_sector(0).has_value());
    std::array<std::byte, 16> got;
    TEST_ASSERT_TRUE(store->try_read(100, got).has_value());
    TEST_ASSERT_TRUE(all_are(got, std::byte{0xFF}));
    // The write is at an odd offset, because unencrypted flash accepts writes at any byte.
    std::array<std::byte, 5> data = {
        std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}
    };
    TEST_ASSERT_TRUE(store->try_write(101, data).has_value());
    TEST_ASSERT_TRUE(store->try_read(100, got).has_value());
    TEST_ASSERT_EQUAL_HEX8(0xFF, std::to_integer<int>(got[0]));
    TEST_ASSERT_TRUE(std::ranges::equal(data, std::span(got).subspan(1, 5)));
    TEST_ASSERT_EQUAL_HEX8(0xFF, std::to_integer<int>(got[6]));
    // The partition is unencrypted, so writes can start at any byte, and the bytes that are stored
    // are the bytes that are read.
    TEST_ASSERT_FALSE(part->encrypted());
    TEST_ASSERT_EQUAL_size_t(1, store->write_size());
    std::array<std::byte, 16> raw;
    TEST_ASSERT_TRUE(store->try_read_raw(100, raw).has_value());
    TEST_ASSERT_TRUE(std::ranges::equal(got, raw));
    TEST_ASSERT_TRUE(store->try_erase_sector(0).has_value());
}
