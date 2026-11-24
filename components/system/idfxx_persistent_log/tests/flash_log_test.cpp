// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_persistent_log: the flash log, on a RAM sector store that follows the rules
// of NOR flash. The sectors are small, so the ring wraps after a few hundred bytes.

#include "../src/bytes.hpp"
#include "../src/entry_buffer.hpp"
#include "../src/flash_log.hpp"
#include "../src/sector_store.hpp"
#include "counting_store.hpp"
#include "entry_helpers.hpp"
#include "esp_rom_crc.h"
#include "unity.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace idfxx::persistent_log;

namespace {

constexpr size_t small_sector = 256;

ram_sector_store make_store(bool encrypted, size_t sectors, size_t sector_size) {
    return encrypted ? ram_sector_store(ram_sector_store::encrypted, sectors, sector_size)
                     : ram_sector_store(sectors, sector_size);
}

// A store that passes each access to another store, but has a write size of 32 bytes, which is more
// than a block.
class wide_store final : public sector_store {
public:
    explicit wide_store(ram_sector_store& inner)
        : sector_store(inner.size(), inner.sector_size(), 32)
        , _inner(&inner) {}

private:
    idfxx::result<void> do_read(size_t offset, idfxx::byte_span dst) const override {
        return _inner->try_read(offset, dst);
    }
    idfxx::result<void> do_read_raw(size_t offset, idfxx::byte_span dst) const override {
        return _inner->try_read_raw(offset, dst);
    }
    idfxx::result<void> do_write(size_t offset, idfxx::byte_view src) override {
        return _inner->try_write(offset, src);
    }
    idfxx::result<void> do_erase_sector(size_t index) override {
        return _inner->try_erase_sector(index);
    }

    ram_sector_store* _inner;
};

// A store with a geometry and no memory behind it, for geometries too large to hold in RAM. Every
// access fails.
class geometry_store final : public sector_store {
public:
    geometry_store(size_t sectors, size_t sector_size)
        : sector_store(sectors * sector_size, sector_size, 1) {}

private:
    idfxx::result<void> do_read(size_t, idfxx::byte_span) const override {
        return idfxx::error(idfxx::errc::not_supported);
    }
    idfxx::result<void> do_read_raw(size_t, idfxx::byte_span) const override {
        return idfxx::error(idfxx::errc::not_supported);
    }
    idfxx::result<void> do_write(size_t, idfxx::byte_view) override {
        return idfxx::error(idfxx::errc::not_supported);
    }
    idfxx::result<void> do_erase_sector(size_t) override {
        return idfxx::error(idfxx::errc::not_supported);
    }
};

// Returns a sector header with the given magic and layout version, and a correct CRC.
std::array<std::byte, 16>
sector_header(const char (&magic)[5], uint16_t version, uint32_t seq, uint16_t flags = 0) {
    std::array<std::byte, 16> h{};
    for (size_t i = 0; i < 4; ++i) {
        h[i] = std::byte(magic[i]);
    }
    h[4] = std::byte(version & 0xFF);
    h[5] = std::byte(version >> 8);
    h[6] = std::byte(flags & 0xFF);
    h[7] = std::byte(flags >> 8);
    for (size_t i = 0; i < 4; ++i) {
        h[8 + i] = std::byte((seq >> (8 * i)) & 0xFF);
    }
    uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(h.data()), 12);
    for (size_t i = 0; i < 4; ++i) {
        h[12 + i] = std::byte((crc >> (8 * i)) & 0xFF);
    }
    return h;
}

// Erases a sector and writes the header `h` at its start through the store, whatever `h` holds.
void put_sector_header(sector_store& store, size_t sector, std::span<const std::byte> h) {
    TEST_ASSERT_TRUE(store.try_erase_sector(sector).has_value());
    TEST_ASSERT_TRUE(store.try_write(sector * store.sector_size(), h).has_value());
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

bool all_erased(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0xFF}; });
}

flash_log open_log(sector_store& store, std::optional<position> hint = std::nullopt) {
    auto flash = flash_log::try_open(store, hint);
    TEST_ASSERT_TRUE(flash.has_value());
    return std::move(*flash);
}

void append(flash_log& flash, std::string_view text) {
    TEST_ASSERT_TRUE(flash.try_append(text).has_value());
}

std::string read_all(const flash_log& flash, size_t chunk = 64) {
    auto r = flash.try_read();
    TEST_ASSERT_TRUE(r.has_value());
    std::string out;
    std::string buf(chunk, '\0');
    while (true) {
        auto n = r->try_next(std::span(buf));
        TEST_ASSERT_TRUE(n.has_value());
        if (*n == 0) {
            break;
        }
        out.append(buf.data(), *n);
    }
    return out;
}

std::string tail_of(const flash_log& flash, size_t n) {
    std::string buf(n, '\0');
    auto got = flash.try_tail(std::span(buf));
    TEST_ASSERT_TRUE(got.has_value());
    buf.resize(*got);
    // A tail reader returns the same bytes, in pieces of any size, and reports its size from the
    // start.
    std::optional<flash_log::tail_reader> t;
    TEST_ASSERT_TRUE(flash.try_read_tail(n, t).has_value());
    TEST_ASSERT_TRUE(t.has_value());
    TEST_ASSERT_EQUAL_size_t(buf.size(), t->size());
    std::string streamed;
    std::array<char, 7> piece;
    while (true) {
        auto k = t->try_next(piece);
        TEST_ASSERT_TRUE(k.has_value());
        if (*k == 0) {
            break;
        }
        streamed.append(piece.data(), *k);
    }
    TEST_ASSERT_TRUE(streamed == buf);
    return buf;
}

// Lines of 11 bytes: "line 00042\n".
std::string lines(int from, int count, const char* word = "line") {
    std::string s;
    char line[32];
    for (int i = from; i < from + count; ++i) {
        int n = std::snprintf(line, sizeof line, "%s %05d\n", word, i);
        s.append(line, static_cast<size_t>(n));
    }
    return s;
}

// Entries of 19 bytes, each a line and two continuations: "e00042\n\tmore\n\tlast\n".
std::string numbered_entries(int from, int count) {
    std::string s;
    char entry[32];
    for (int i = from; i < from + count; ++i) {
        int n = std::snprintf(entry, sizeof entry, "e%05d\n\tmore\n\tlast\n", i);
        s.append(entry, static_cast<size_t>(n));
    }
    return s;
}

// The part of `text` from the first entry that begins after `from`.
std::string from_entry_after(const std::string& text, size_t from) {
    return text.substr(text.find("\ne", from) + 1);
}

void append_lines(flash_log& flash, int from, int count, const char* word = "line") {
    for (int i = from; i < from + count; ++i) {
        append(flash, lines(i, 1, word));
    }
}

void assert_same(const std::string& expected, const std::string& actual) {
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), actual.c_str());
}

} // namespace

// =============================================================================
// Opening, appending and reading
// =============================================================================

TEST_CASE("flash_log opens an erased store empty", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    TEST_ASSERT_FALSE(flash.head().has_value());
    TEST_ASSERT_FALSE(flash.opened_by_hint());
    assert_same("", read_all(flash));
    assert_same("", tail_of(flash, 100));
}

TEST_CASE("flash_log refuses a store with too few or odd-sized sectors", "[persistent_log]") {
    ram_sector_store one(1, small_sector);
    TEST_ASSERT_FALSE(flash_log::try_open(one).has_value());
    ram_sector_store tiny(8, 32);
    TEST_ASSERT_FALSE(flash_log::try_open(tiny).has_value());
    // The store is refused for its geometry, before any read.
    geometry_store huge(2, 0x20000);
    auto h = flash_log::try_open(huge);
    TEST_ASSERT_FALSE(h.has_value());
    TEST_ASSERT_TRUE(h.error() == idfxx::errc::invalid_arg);
    // Sectors that aren't a whole number of blocks, and writes wider than a block, are refused.
    ram_sector_store odd(8, 72);
    auto r = flash_log::try_open(odd);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    ram_sector_store inner(8, small_sector);
    wide_store wide(inner);
    r = flash_log::try_open(wide);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    // A sector needs room for at least three blocks of records after the header.
    ram_sector_store small(8, sector_header_size + 32);
    TEST_ASSERT_FALSE(flash_log::try_open(small).has_value());
    ram_sector_store enc(ram_sector_store::encrypted, 8, sector_header_size + 48);
    TEST_ASSERT_TRUE(flash_log::try_open(enc).has_value());
}

TEST_CASE("flash_log reads back what was appended, in any chunk size", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    std::string text = lines(0, 12);
    append_lines(flash, 0, 12);
    assert_same(text, read_all(flash));
    assert_same(text, read_all(flash, 1));
    assert_same(text, read_all(flash, 7));
    assert_same(text, read_all(flash, 4096));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("flash_log splits text that doesn't fit across sectors", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    std::string text = lines(0, 90); // 990 bytes: 218 per record, so 5 sectors
    append(flash, text);
    TEST_ASSERT_EQUAL_UINT32(4, flash.head()->sector);
    TEST_ASSERT_EQUAL_UINT32(4, flash.head()->seq);
    assert_same(text, read_all(flash));
    assert_same(text, read_all(open_log(store)));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("flash_log drops the oldest text when the ring wraps", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    // 14 records of 16 bytes fill the 224 bytes that a sector has for records, so each record holds
    // 10 bytes of text and each sector holds 140. 280 lines fill 20 sectors, which goes around the
    // ring 2.5 times.
    std::string all;
    for (int i = 0; i < 280; ++i) {
        std::string line = lines(i, 1).substr(0, 10);
        line.back() = '\n';
        append(flash, line);
        all += line;
    }
    TEST_ASSERT_EQUAL_UINT32(3, flash.head()->sector);
    TEST_ASSERT_EQUAL_UINT32(19, flash.head()->seq);
    std::string got = read_all(flash);
    // The log holds sectors 4 to 7 and 0 to 3: seven full sectors and the head. The oldest
    // sector's first line is left out, because the oldest sector of a wrapped ring may begin
    // partway through a line.
    TEST_ASSERT_EQUAL_size_t(8 * 140 - 10, got.size());
    TEST_ASSERT_TRUE(all.ends_with(got));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE(
    "flash_log read of a wrapped ring begins at an entry, in any chunk size",
    "[persistent_log]"
) {
    for (bool encrypted : {false, true}) {
        ram_sector_store store = make_store(encrypted, 8, small_sector);
        auto flash = open_log(store);
        // One append of 3610 bytes, at 218 bytes to a sector, fills 17 sectors, and the ring keeps
        // the last 8. The oldest of those begins partway through an entry's first line, and that
        // entry's continuations follow it.
        const std::string text = numbered_entries(0, 190);
        append(flash, text);
        TEST_ASSERT_EQUAL_UINT32(16, flash.head()->seq);
        const size_t oldest = 9 * 218;
        TEST_ASSERT_NOT_EQUAL('\n', text[oldest - 1]);
        TEST_ASSERT_EQUAL('\t', text[text.find('\n', oldest) + 1]);
        const std::string expected = from_entry_after(text, oldest);
        TEST_ASSERT_TRUE(expected.starts_with("e"));
        for (size_t chunk : {1, 7, 64, 4096}) {
            assert_same(expected, read_all(flash, chunk));
        }
        assert_same(expected, read_all(open_log(store), 7));
    }
}

TEST_CASE("flash_log read gives a log that never wrapped as it was appended", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    // The text begins with a continuation line and runs over three sectors, but it is still read
    // back completely.
    const std::string text = "\tled by a tab\n" + numbered_entries(0, 30) + "partial";
    append(flash, text);
    TEST_ASSERT_EQUAL_UINT32(2, flash.head()->seq);
    for (size_t chunk : {1, 7, 4096}) {
        assert_same(text, read_all(flash, chunk));
    }
    // After the ring has wrapped, the text appended after a clear is also read back completely.
    append(flash, numbered_entries(100, 200));
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    append(flash, "\tafter the clear\nmore");
    for (size_t chunk : {1, 7, 4096}) {
        assert_same("\tafter the clear\nmore", read_all(flash, chunk));
    }
}

TEST_CASE(
    "flash_log finds the head by sequence number, not index, after a wrap",
    "[persistent_log]"
) {
    ram_sector_store store(8, small_sector);
    position before;
    std::string got;
    {
        auto flash = open_log(store);
        append(flash, lines(0, 190)); // 2090 bytes in 10 sectors, so the head is at index 1
        append(flash, "last\n");
        before = *flash.head();
        got = read_all(flash);
    }
    TEST_ASSERT_EQUAL_UINT32(1, before.sector);
    auto flash = open_log(store);
    TEST_ASSERT_FALSE(flash.opened_by_hint());
    TEST_ASSERT_TRUE(flash.head() == before);
    assert_same(got, read_all(flash));
    TEST_ASSERT_TRUE(got.ends_with("last\n"));
    append(flash, "more\n");
    assert_same(got + "more\n", read_all(flash));
}

TEST_CASE("flash_log continues where it left off after a full scan", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    position before;
    {
        auto flash = open_log(store);
        append_lines(flash, 0, 25);
        before = *flash.head();
    }
    auto flash = open_log(store);
    TEST_ASSERT_FALSE(flash.opened_by_hint());
    TEST_ASSERT_TRUE(flash.head() == before);
    append_lines(flash, 25, 25);
    assert_same(lines(0, 50), read_all(flash));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("flash_log fails to open when a read fails", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    store.cut_after(0);
    TEST_ASSERT_FALSE(store.try_write(0, std::as_bytes(std::span("x", 1))).has_value());
    TEST_ASSERT_FALSE(flash_log::try_open(store).has_value());
}

// =============================================================================
// Hints
// =============================================================================

TEST_CASE("flash_log opens by a good hint without scanning every sector", "[persistent_log]") {
    ram_sector_store store(16, small_sector);
    position hint;
    {
        auto flash = open_log(store);
        append(flash, lines(0, 40));
        hint = *flash.head();
    }
    store.reset_counts();
    auto full = open_log(store);
    size_t full_reads = store.reads();
    TEST_ASSERT_TRUE(full.head() == hint);

    store.reset_counts();
    auto flash = open_log(store, hint);
    TEST_ASSERT_TRUE(flash.opened_by_hint());
    TEST_ASSERT_TRUE(flash.head() == hint);
    TEST_ASSERT_LESS_THAN_size_t(full_reads - 10, store.reads());
    append(flash, "next\n");
    assert_same(lines(0, 40) + "next\n", read_all(flash));
}

TEST_CASE("flash_log ignores a hint that later appends have passed", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append_lines(flash, 0, 3);
    position same_sector = *flash.head();
    append_lines(flash, 3, 3);
    position now = *flash.head();
    TEST_ASSERT_EQUAL_UINT32(same_sector.sector, now.sector);

    auto reopened = open_log(store, same_sector);
    TEST_ASSERT_FALSE(reopened.opened_by_hint());
    TEST_ASSERT_TRUE(reopened.head() == now);

    // The clear moves the log on to the next sector, and leaves the hint's sector with erased bytes
    // at its end.
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    append(flash, "after\n");
    auto cleared = open_log(store, now);
    TEST_ASSERT_FALSE(cleared.opened_by_hint());
    TEST_ASSERT_TRUE(cleared.head() == flash.head());
    assert_same("after\n", read_all(cleared));
}

TEST_CASE("flash_log ignores a hint that doesn't hold", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    position good;
    {
        auto flash = open_log(store);
        append_lines(flash, 0, 5);
        good = *flash.head();
    }
    std::vector<position> bad = {
        {.sector = 99, .offset = good.offset, .seq = good.seq},
        {.sector = good.sector, .offset = 3, .seq = good.seq},
        {.sector = good.sector, .offset = good.offset, .seq = good.seq + 1},
        {.sector = good.sector + 1, .offset = sector_header_size, .seq = good.seq + 1},
        // Past the end of the records, which would leave a gap of erased bytes that the reader
        // stops at.
        {.sector = good.sector, .offset = good.offset + 16, .seq = good.seq},
        // Before the end of the records, which would write over the last record.
        {.sector = good.sector, .offset = good.offset - 16, .seq = good.seq},
        // Not at the start of a block.
        {.sector = good.sector, .offset = good.offset + 4, .seq = good.seq},
        {.sector = good.sector, .offset = good.offset - 1, .seq = good.seq},
        {.sector = good.sector, .offset = small_sector + 1, .seq = good.seq},
    };
    for (const auto& hint : bad) {
        auto flash = open_log(store, hint);
        TEST_ASSERT_FALSE(flash.opened_by_hint());
        TEST_ASSERT_TRUE(flash.head() == good);
    }
    auto flash = open_log(store, good);
    TEST_ASSERT_TRUE(flash.opened_by_hint());
}

// =============================================================================
// Power cuts
// =============================================================================

TEST_CASE("flash_log moves on from a record a power cut tore", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append_lines(flash, 0, 4);
    position hint = *flash.head();

    store.cut_after(record_header_size + 3);
    TEST_ASSERT_FALSE(flash.try_append("this line is torn\n").has_value());
    store.restore_power();

    auto reopened = open_log(store);
    TEST_ASSERT_EQUAL_UINT32(hint.sector, reopened.head()->sector);
    TEST_ASSERT_EQUAL_UINT32(small_sector, reopened.head()->offset);
    auto hinted = open_log(store, hint);
    TEST_ASSERT_FALSE(hinted.opened_by_hint());
    TEST_ASSERT_TRUE(hinted.head() == reopened.head());
    // A hint at the end of the torn sector is still accepted, because the next record goes in a
    // new sector anyway.
    auto moved = open_log(store, reopened.head());
    TEST_ASSERT_TRUE(moved.opened_by_hint());

    assert_same(lines(0, 4), read_all(reopened));
    append(reopened, "after\n");
    TEST_ASSERT_EQUAL_UINT32(hint.sector + 1, reopened.head()->sector);
    assert_same(lines(0, 4) + "after\n", read_all(reopened));
    assert_same(lines(0, 4) + "after\n", read_all(open_log(store)));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("flash_log reads on from a torn record at the start of a line", "[persistent_log]") {
    for (size_t chunk : {1, 3, 7, 64}) {
        ram_sector_store store(8, small_sector);
        auto flash = open_log(store);
        append(flash, "e00000\n\tmore\n");
        append(flash, "e00001 cut");
        store.cut_after(record_header_size + 3);
        TEST_ASSERT_FALSE(flash.try_append(" short\n").has_value());
        store.restore_power();

        // The next record goes in a new sector, and the reader treats it as the start of a line.
        // The reader ends the line that was cut short, leaves out the lines that start with a tab,
        // and keeps the next entry.
        auto reopened = open_log(store);
        append(reopened, "\tcont\n\tcont\ne00002\n\tmore\n");
        assert_same("e00000\n\tmore\ne00001 cut\ne00002\n\tmore\n", read_all(reopened, chunk));
    }
}

TEST_CASE("flash_log end_line ends a last line cut short", "[persistent_log]") {
    {
        // With an empty log, nothing is written.
        ram_sector_store store(8, small_sector);
        auto flash = open_log(store);
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        TEST_ASSERT_FALSE(flash.head().has_value());
        assert_same("", read_all(flash));
    }
    {
        // When the last line has already ended, or the log has been cleared since, nothing is
        // appended.
        ram_sector_store store(8, small_sector);
        auto flash = open_log(store);
        append(flash, "e00000\n");
        const position before = *flash.head();
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        TEST_ASSERT_TRUE(flash.head() == before);
        append(flash, "e00001");
        TEST_ASSERT_TRUE(flash.try_clear().has_value());
        const position cleared = *flash.head();
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        TEST_ASSERT_TRUE(flash.head() == cleared);
        assert_same("", read_all(flash));
    }
    {
        // When the log ends partway through a line, a newline is appended, and only once.
        ram_sector_store store(8, small_sector);
        auto flash = open_log(store);
        append(flash, "e00000\ne00001 cut");
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        append(flash, "e00002\n");
        assert_same("e00000\ne00001 cut\ne00002\n", read_all(flash));
    }
    {
        // The log ends partway through a line at the end of a full sector, and a power cut tore
        // the header of the next sector. The newline goes in a new sector, and opening by a scan
        // or by a hint finds the same head.
        const std::string text = numbered_entries(0, 11) + "e00011 cu";
        for (bool hinted : {false, true}) {
            ram_sector_store store(8, small_sector);
            auto flash = open_log(store);
            append(flash, text);
            TEST_ASSERT_EQUAL_UINT32(small_sector, flash.head()->offset);
            const position full = *flash.head();
            store.cut_after(5);
            TEST_ASSERT_FALSE(flash.try_append("after\n").has_value());
            store.restore_power();
            auto reopened = hinted ? open_log(store, full) : open_log(store);
            TEST_ASSERT_TRUE(reopened.head() == full);
            TEST_ASSERT_TRUE(reopened.try_end_line().has_value());
            append(reopened, "after\n");
            assert_same(text + "\nafter\n", read_all(reopened));
            TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
        }
    }
    {
        // After a torn record, the reader ends the line itself, so end_line() appends nothing.
        ram_sector_store store(8, small_sector);
        auto flash = open_log(store);
        append(flash, "e00000\ne00001 cut");
        store.cut_after(record_header_size + 3);
        TEST_ASSERT_FALSE(flash.try_append(" short\n").has_value());
        store.restore_power();
        auto reopened = open_log(store);
        const position torn = *reopened.head();
        TEST_ASSERT_TRUE(reopened.try_end_line().has_value());
        TEST_ASSERT_TRUE(reopened.head() == torn);
        append(reopened, "e00002\n");
        assert_same("e00000\ne00001 cut\ne00002\n", read_all(reopened));
    }
}

TEST_CASE("flash_log moves on from text written without its record header", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    position hint;
    {
        auto flash = open_log(store);
        append_lines(flash, 0, 4);
        hint = *flash.head();
    }
    // The header is erased but bytes after it were written.
    store.bytes()[hint.sector * small_sector + hint.offset + record_header_size + 2] =
        std::byte{'x'};
    auto flash = open_log(store, hint);
    TEST_ASSERT_FALSE(flash.opened_by_hint());
    TEST_ASSERT_EQUAL_UINT32(small_sector, flash.head()->offset);
    assert_same(lines(0, 4), read_all(flash));
    append(flash, "after\n");
    assert_same(lines(0, 4) + "after\n", read_all(flash));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("flash_log treats a sector whose header a power cut tore as unused", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append_lines(flash, 0, 4);
    position before = *flash.head();

    store.cut_after(5);
    TEST_ASSERT_FALSE(flash.try_clear().has_value());
    store.restore_power();

    auto reopened = open_log(store);
    TEST_ASSERT_TRUE(reopened.head() == before);
    // The power cut came after the header's magic and part of its version field were written. The
    // header is torn, not a newer layout.
    TEST_ASSERT_FALSE(reopened.found_newer_format());
    assert_same(lines(0, 4), read_all(reopened));
    TEST_ASSERT_TRUE(open_log(store, before).opened_by_hint());
    TEST_ASSERT_TRUE(reopened.try_clear().has_value());
    TEST_ASSERT_EQUAL_UINT32(before.sector + 1, reopened.head()->sector);
    TEST_ASSERT_EQUAL_UINT32(before.seq + 1, reopened.head()->seq);
    append(reopened, "after\n");
    assert_same("after\n", read_all(open_log(store)));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

namespace {

// Returns whether each line of @p text starts as lines() makes it, with "line " or "old " and a
// digit. Only the last line may be shortened, as an append that a power cut stopped leaves it. The
// shortened line must keep at least the first character of its word.
bool whole_lines(std::string_view text) {
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        const std::string_view number = line.substr(
            line.find(' ') == std::string_view::npos ? line.size() : line.find(' ') + 1
        );
        const std::string_view word = line.substr(0, line.size() - number.size());
        const bool last = text.empty();
        if (word != "line " && word != "old " &&
            !(last && line.size() > 0 &&
              (std::string_view("line ").starts_with(line) ||
               std::string_view("old ").starts_with(line)))) {
            return false;
        }
        if (number.size() > 5 || (!last && number.size() != 5) ||
            !std::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; })) {
            return false;
        }
    }
    return true;
}

void power_cut_sweep(bool encrypted) {
    // Six 128-byte sectors, filled until the ring has wrapped at least once, so that starting a
    // sector erases old text.
    constexpr size_t sectors = 6;
    constexpr size_t sector = 128;
    ram_sector_store base = make_store(encrypted, sectors, sector);
    std::string committed;
    position base_hint;
    {
        auto flash = open_log(base);
        for (int i = 0; i < 50; ++i) {
            append(flash, lines(i, 1, "old"));
            committed += lines(i, 1, "old");
        }
        base_hint = *flash.head();
    }

    // Each step is an append, or a clear when the step's text is empty. Together, the steps split
    // text across sectors, move around the ring, and clear the log.
    std::vector<std::string> steps = {
        lines(0, 1),
        lines(1, 25), // 275 bytes, over three sectors
        lines(26, 1),
        "",
        lines(27, 1),
        lines(28, 12),
        lines(40, 1),
    };
    // The ring keeps at least the four sectors other than the head and the sector being started.
    // Each of those holds at least 3 records of 11 bytes, and each record takes 32 bytes.
    constexpr size_t floor = (sectors - 2) * 33;

    size_t runs = 0;
    size_t erase_cuts = 0;
    bool completed = false;
    for (size_t budget = 0; budget < 20000 && !completed; ++budget) {
        ram_sector_store store = base;
        auto flash = open_log(store, base_hint);
        TEST_ASSERT_TRUE(flash.opened_by_hint());
        std::string expected = committed;
        std::optional<position> last_hint = base_hint;
        std::string in_flight;

        store.cut_after(budget);
        completed = true;
        for (const auto& step : steps) {
            auto r = step.empty() ? flash.try_clear() : flash.try_append(step);
            if (!r) {
                in_flight = step;
                completed = false;
                break;
            }
            expected = step.empty() ? std::string() : expected + step;
            last_hint = flash.head();
        }
        if (completed) {
            break;
        }
        ++runs;
        erase_cuts += store.cut_erasing() ? 1 : 0;
        store.restore_power();

        // Every way of opening finds the same head.
        auto scanned = open_log(store);
        auto by_base = open_log(store, base_hint);
        auto by_last = open_log(store, last_hint);
        TEST_ASSERT_TRUE(scanned.head() == by_base.head());
        TEST_ASSERT_TRUE(scanned.head() == by_last.head());
        // A header that the power cut tore is read as torn, never as a newer layout.
        TEST_ASSERT_FALSE(scanned.found_newer_format());
        TEST_ASSERT_FALSE(by_base.found_newer_format());
        TEST_ASSERT_FALSE(by_last.found_newer_format());

        // Once the last line is ended, the log reads as the newest part of the committed text,
        // starting at the start of a line, followed by some leading part of the step that was in
        // flight, in whole records. A newline follows that part if it ends partway through a line.
        // The reader ends a line that a torn record cut short, and end_line() ends a line cut
        // short in any other way. Nothing is read from a sector whose erase the power cut
        // interrupted. No line is a fragment, so each line starts as lines() makes it.
        TEST_ASSERT_TRUE(scanned.try_end_line().has_value());
        std::string got = read_all(scanned);
        bool matched = false;
        for (size_t j = in_flight.size() + 1; j-- > 0 && !matched;) {
            std::string whole = expected + in_flight.substr(0, j);
            if (!whole.empty() && whole.back() != '\n') {
                whole += '\n';
            }
            const bool from_line = got.size() == whole.size() ||
                (got.size() < whole.size() && whole[whole.size() - got.size() - 1] == '\n');
            matched =
                whole.ends_with(got) && from_line && got.size() >= std::min(whole.size(), floor);
        }
        matched = matched && whole_lines(got);
        if (!matched) {
            std::printf(
                "%s budget %u: unexpected log '%s'\n",
                encrypted ? "encrypted" : "plain",
                unsigned(budget),
                got.c_str()
            );
        }
        TEST_ASSERT_TRUE(matched);

        // Appending again works, and nothing is ever written over bytes that weren't erased.
        append(scanned, "after\n");
        std::string again = read_all(scanned);
        TEST_ASSERT_TRUE(again.ends_with(got.substr(got.size() / 2) + "after\n"));
        assert_same(again, read_all(open_log(store, scanned.head())));
        TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    }
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_GREATER_THAN_size_t(500, runs);
    TEST_ASSERT_GREATER_THAN_size_t(0, erase_cuts);
}

} // namespace

TEST_CASE(
    "flash_log stays readable and consistent with a power cut at every byte",
    "[persistent_log]"
) {
    power_cut_sweep(false);
}

TEST_CASE(
    "flash_log stays readable and consistent with a power cut at every byte, encrypted",
    "[persistent_log]"
) {
    power_cut_sweep(true);
}

namespace {

// Returns whether each line of @p text is a line that lines() makes, or a filler line of "fill"
// followed by 'x's.
bool test_lines(std::string_view text) {
    while (!text.empty()) {
        const size_t end = text.find('\n');
        if (end == std::string_view::npos) {
            return false;
        }
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end + 1);
        const bool numbered = line.size() == 10 && line.starts_with("line ") &&
            std::ranges::all_of(line.substr(5), [](char c) { return c >= '0' && c <= '9'; });
        const bool fill = line.starts_with("fill") &&
            std::ranges::all_of(line.substr(4), [](char c) { return c == 'x'; });
        if (!numbered && !fill) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE(
    "flash_log reads nothing of a sector whose erase a power cut stopped",
    "[persistent_log]"
) {
    size_t erase_cuts = 0;
    for (bool encrypted : {false, true}) {
        for (int shift = 0; shift < 8; ++shift) {
            // Four sectors, filled until the ring has wrapped. The text is appended in runs of
            // lines that split lines across sectors, so that sectors begin partway through a line.
            ram_sector_store base = make_store(encrypted, 4, small_sector);
            std::string committed;
            int next = 0;
            {
                auto flash = open_log(base);
                for (int i = 0; i < 16; ++i) {
                    const int count = 3 + (i + shift) % 5;
                    append(flash, lines(next, count));
                    committed += lines(next, count);
                    next += count;
                }
                // Then append a line that fills the head sector exactly. The log ends at the end
                // of a line, so nothing ends the line again, and the next append starts the oldest
                // sector.
                if (flash.head()->offset < small_sector) {
                    const size_t room = small_sector - flash.head()->offset - record_header_size;
                    const std::string fill = "fill" + std::string(room - 5, 'x') + "\n";
                    append(flash, fill);
                    committed += fill;
                }
                TEST_ASSERT_EQUAL_UINT32(small_sector, flash.head()->offset);
            }

            // Cut the power at each byte of starting that sector: in its retire mark, at its
            // erase, and then in its header. The log is read before anything more is appended, and
            // again after.
            for (size_t budget = 0; budget <= 2 * block_size; ++budget) {
                ram_sector_store store = base;
                {
                    auto flash = open_log(store);
                    store.cut_after(budget);
                    TEST_ASSERT_FALSE(flash.try_append(lines(90000, 1)).has_value());
                }
                erase_cuts += store.cut_erasing() ? 1 : 0;
                store.restore_power();
                for (int pass = 0; pass < 2; ++pass) {
                    auto reopened = open_log(store);
                    TEST_ASSERT_TRUE(reopened.try_end_line().has_value());
                    std::string whole = committed;
                    if (pass == 1) {
                        append(reopened, lines(next, 1));
                        whole += lines(next, 1);
                    }
                    const std::string got = read_all(reopened);
                    // The log reads as the newest part of the committed text, starting at the
                    // start of a line, and holds only lines as they were appended. It holds no
                    // part of the sector being started, and no fragment of a line.
                    const bool from_line = got.size() == whole.size() ||
                        (got.size() < whole.size() && whole[whole.size() - got.size() - 1] == '\n');
                    const bool ok =
                        !got.empty() && whole.ends_with(got) && from_line && test_lines(got);
                    if (!ok) {
                        std::printf(
                            "%s shift %d budget %u pass %d: unexpected log '%s'\n",
                            encrypted ? "encrypted" : "plain",
                            shift,
                            unsigned(budget),
                            pass,
                            got.c_str()
                        );
                    }
                    TEST_ASSERT_TRUE(ok);
                }
                TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_size_t(0, erase_cuts);
}

TEST_CASE(
    "flash_log takes a sector whose retire mark is written, whole or torn, as unused",
    "[persistent_log]"
) {
    for (bool encrypted : {false, true}) {
        // Four full sectors, with one line per record and 7 records to a sector. The next append
        // starts sector 0 again, because it is the oldest.
        ram_sector_store base = make_store(encrypted, 4, small_sector);
        position head;
        {
            auto flash = open_log(base);
            append_lines(flash, 0, 28);
            head = *flash.head();
        }
        TEST_ASSERT_EQUAL_UINT32(3, head.sector);
        TEST_ASSERT_EQUAL_UINT32(small_sector, head.offset);
        // The text that the log reads with sector 0 erased.
        ram_sector_store erased = base;
        TEST_ASSERT_TRUE(erased.try_erase_sector(0).has_value());
        const std::string without = read_all(open_log(erased));
        TEST_ASSERT_TRUE(lines(0, 28).ends_with(without));
        TEST_ASSERT_EQUAL(std::string::npos, without.find(lines(6, 1)));

        // Cut the power 5 bytes into the retire mark, and then, once the mark is complete, at the
        // erase, which erases only the second half of the sector.
        for (size_t budget : {size_t{5}, block_size}) {
            ram_sector_store store = base;
            {
                auto flash = open_log(store, head);
                store.cut_after(budget);
                TEST_ASSERT_FALSE(flash.try_append("after\n").has_value());
            }
            TEST_ASSERT_EQUAL(budget == block_size, store.cut_erasing());
            store.restore_power();
            // The header is as it was, the retire mark isn't erased, and the sector's records are
            // there up to where the erase reached.
            TEST_ASSERT_TRUE(read_raw_block(store, 0) == read_raw_block(base, 0));
            TEST_ASSERT_FALSE(all_erased(read_raw_block(store, block_size)));
            TEST_ASSERT_TRUE(
                read_raw_block(store, sector_header_size) ==
                read_raw_block(base, sector_header_size)
            );

            // Opened by scan or by hint, the log reads as if the sector were erased.
            auto scanned = open_log(store);
            TEST_ASSERT_TRUE(scanned.head() == head);
            assert_same(without, read_all(scanned));
            auto hinted = open_log(store, head);
            TEST_ASSERT_TRUE(hinted.opened_by_hint());
            assert_same(without, read_all(hinted));
            assert_same(without, tail_of(hinted, 2000));

            // Starting the sector again erases it, without writing over its mark.
            TEST_ASSERT_TRUE(scanned.try_end_line().has_value());
            append(scanned, "after\n");
            TEST_ASSERT_EQUAL_UINT32(0, scanned.head()->sector);
            assert_same(without + "after\n", read_all(scanned));
            assert_same(without + "after\n", read_all(open_log(store)));
            TEST_ASSERT_TRUE(all_erased(read_raw_block(store, block_size)));
            TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
        }
    }
}

// =============================================================================
// Clearing
// =============================================================================

TEST_CASE("flash_log clear leaves only what is appended after it", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append_lines(flash, 0, 30, "before");
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    assert_same("", read_all(flash));
    assert_same("", tail_of(flash, 100));
    append_lines(flash, 0, 20, "after");
    assert_same(lines(0, 20, "after"), read_all(flash));
    assert_same(lines(0, 20, "after"), tail_of(flash, 1000));
    assert_same(lines(0, 20, "after"), read_all(open_log(store)));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("flash_log clear holds after the ring wraps past it", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append(flash, lines(0, 100, "before"));
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    std::string after;
    for (int i = 0; i < 300; ++i) {
        append(flash, lines(i, 1, "after"));
        after += lines(i, 1, "after");
        std::string got = read_all(flash);
        TEST_ASSERT_TRUE(after.ends_with(got));
        TEST_ASSERT_EQUAL(std::string::npos, got.find("before"));
    }
    std::string got = read_all(open_log(store));
    TEST_ASSERT_TRUE(after.ends_with(got));
    // Every sector except the head holds at least 7 lines of 12 bytes, each in a 32-byte record.
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(7 * 7 * 12, got.size());
}

TEST_CASE("flash_log reads from the newest clear", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append(flash, "first\n");
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    append(flash, "second\n");
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    append(flash, "third\n");
    assert_same("third\n", read_all(flash));
    assert_same("third\n", read_all(open_log(store)));
}

TEST_CASE("flash_log clear works on an empty store", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    assert_same("", read_all(flash));
    append(flash, "one\n");
    assert_same("one\n", read_all(open_log(store)));
}

// =============================================================================
// The tail
// =============================================================================

TEST_CASE("flash_log tail gives the newest bytes from the start of a line", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    std::string text = lines(0, 50);
    append_lines(flash, 0, 50);

    assert_same(text, tail_of(flash, 10000));
    assert_same(lines(48, 2), tail_of(flash, 22));
    assert_same(lines(48, 2), tail_of(flash, 25));
    assert_same(lines(49, 1), tail_of(flash, 21));
    assert_same("", tail_of(flash, 10));
    // The tail can span several sectors.
    std::string tail = tail_of(flash, 300);
    assert_same(lines(23, 27), tail);
}

TEST_CASE("flash_log tail of a wrapped ring begins after its first line", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append(flash, lines(0, 300));
    std::string all = read_all(flash);
    // The oldest sector of a wrapped ring may begin partway through a line, so the tail leaves
    // its first line out, as read does.
    TEST_ASSERT_TRUE(all.starts_with("line "));
    TEST_ASSERT_TRUE(lines(0, 300).ends_with(all));
    assert_same(all, tail_of(flash, 100000));
    assert_same(lines(290, 10), tail_of(flash, 110));
}

TEST_CASE(
    "flash_log tail begins at an entry, past the lines that continue one",
    "[persistent_log]"
) {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append(flash, "1 one\n\ta\n\tb\n2 two\n\tc\n"); // 21 bytes
    assert_same("1 one\n\ta\n\tb\n2 two\n\tc\n", tail_of(flash, 21));
    // The requested size reaches back to the start of a line that continues an entry.
    assert_same("2 two\n\tc\n", tail_of(flash, 15));
    // The requested size reaches back to partway through a line, and more lines of its entry
    // follow it.
    assert_same("2 two\n\tc\n", tail_of(flash, 14));
    assert_same("2 two\n\tc\n", tail_of(flash, 13));
    // When the requested size holds only an entry's continuation lines, the tail is empty.
    assert_same("", tail_of(flash, 3));
    assert_same("", tail_of(flash, 5));
}

TEST_CASE("flash_log tail of a wrapped ring begins at an entry", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    // Entries of 18 bytes, each a line and a continuation: "e00042\n\tcontinued\n".
    std::string text;
    char entry[32];
    for (int i = 0; i < 200; ++i) {
        int n = std::snprintf(entry, sizeof entry, "e%05d\n\tcontinued\n", i);
        text.append(entry, static_cast<size_t>(n));
    }
    append(flash, text);
    std::string all = read_all(flash);
    // The oldest sector's first line is followed by a continuation line. The tail leaves out both
    // lines, as read does.
    const size_t oldest = 9 * 218;
    TEST_ASSERT_EQUAL('\t', text[text.find('\n', oldest) + 1]);
    assert_same(from_entry_after(text, oldest), all);
    assert_same(all, tail_of(flash, 100000));
    // When the requested size reaches back to partway through an entry's first line, the tail
    // also leaves out that entry's continuation.
    assert_same(text.substr(text.size() - 3 * 18), tail_of(flash, 3 * 18 + 15));
}

TEST_CASE("flash_log works with 4 KiB sectors", "[persistent_log]") {
    ram_sector_store store(4, 4096);
    auto flash = open_log(store);
    std::string text = lines(0, 900); // 9900 bytes: three sectors
    append(flash, text);
    TEST_ASSERT_EQUAL_UINT32(2, flash.head()->sector);
    assert_same(text, read_all(flash, 1024));
    assert_same(lines(800, 100), tail_of(flash, 1100));
    auto reopened = open_log(store, flash.head());
    TEST_ASSERT_TRUE(reopened.opened_by_hint());
    append(reopened, text); // wraps, dropping the first sector
    std::string got = read_all(reopened, 1024);
    TEST_ASSERT_TRUE((text + text).ends_with(got));
    TEST_ASSERT_GREATER_THAN_size_t(3 * 4000, got.size());
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

// =============================================================================
// Reading while the log is appended to
// =============================================================================

namespace {

// Each 11-byte line takes a 17-byte record, padded to 32 bytes, and 7 records fill a sector of this
// size exactly. So no line is split across sectors, and each full sector holds 77 bytes of text.
constexpr size_t line_sector = sector_header_size + 7 * 32;
constexpr size_t text_per_sector = 77;

// Reads @p before bytes of the log, or a little more because it reads in calls of @p chunk bytes,
// then runs @p meanwhile and reads the rest. Returns the text, and the number of bytes read before
// @p meanwhile ran.
template<typename F>
std::pair<std::string, size_t>
read_across(const flash_log& flash, size_t chunk, size_t before, F meanwhile) {
    auto r = flash.try_read();
    TEST_ASSERT_TRUE(r.has_value());
    std::string out;
    std::string buf(chunk, '\0');
    bool done = false;
    while (out.size() < before) {
        auto n = r->try_next(std::span(buf));
        TEST_ASSERT_TRUE(n.has_value());
        if (*n == 0) {
            done = true;
            break;
        }
        out.append(buf.data(), *n);
    }
    const size_t given = out.size();
    meanwhile();
    while (!done) {
        auto n = r->try_next(std::span(buf));
        TEST_ASSERT_TRUE(n.has_value());
        if (*n == 0) {
            break;
        }
        out.append(buf.data(), *n);
    }
    return {out, given};
}

} // namespace

TEST_CASE("flash_log reader ends where the head was when it began", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        for (size_t chunk : {1, 7, 64, 4096}) {
            for (size_t before : {0, 1, 50, 77, 100, 150}) {
                ram_sector_store store = make_store(encrypted, 8, line_sector);
                auto flash = open_log(store);
                // The log fills two sectors and part of a third, so later appends go first to the
                // head sector, and then to new sectors.
                append_lines(flash, 0, 17);
                const std::string full = read_all(flash);
                auto [got, given] = read_across(flash, chunk, before, [&] {
                    append_lines(flash, 1000, 20, "newr");
                });
                assert_same(full, got);
            }
        }
    }
}

TEST_CASE("flash_log reader skips sectors recycled while it reads", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        // A wrapped ring of 4 sectors, with three full sectors (231 bytes) and then the head. The
        // appends made while the reader reads are lines of the same length, so a sector that is
        // started again has valid records at the same offsets as before. Only its sequence number
        // tells it apart from the old sector.
        for (size_t chunk : {1, 5, 11, 64, 200, 4096}) {
            for (size_t before : {0, 1, 50, 66, 76, 77, 78, 143, 150, 154, 200, 260}) {
                for (int recycled = 0; recycled <= 4; ++recycled) {
                    ram_sector_store store = make_store(encrypted, 4, line_sector);
                    auto flash = open_log(store);
                    append_lines(flash, 0, 7 * 5 + 3);
                    // The ring holds lines 14 to 37, and read leaves out the oldest sector's first
                    // line.
                    const std::string held = lines(14, 7 * 3 + 3);
                    const std::string full = read_all(flash);
                    assert_same(held.substr(11), full);
                    const uint32_t seq = flash.head()->seq;
                    auto [got, given] = read_across(flash, chunk, before, [&] {
                        // Fill the head sector with 4 more lines, then start `recycled` more
                        // sectors, each of which erases the oldest sector.
                        const int more = 4 + (recycled > 0 ? 7 * (recycled - 1) + 1 : 0);
                        append_lines(flash, 2000, more, "newr");
                        TEST_ASSERT_EQUAL_UINT32(recycled, flash.head()->seq - seq);
                    });
                    TEST_ASSERT_TRUE(got.find("newr") == std::string::npos);
                    // The reader returns what it had already read, then the log from where the
                    // reader was, if that sector wasn't recycled. If it was, the reader ends the
                    // line that it was in, and resumes at an entry in the oldest sector that wasn't
                    // recycled. It leaves out that sector's first line, as a reader that hadn't
                    // read anything yet does.
                    const size_t oldest = static_cast<size_t>(recycled) * text_per_sector;
                    const bool gap = given == 0 || 11 + given < oldest;
                    const size_t from = gap ? oldest + 11 : 11 + given;
                    const bool cut = given > 0 && gap && given % 11 != 0;
                    const std::string expected = held.substr(11, given) + (cut ? "\n" : "") +
                                                 (from < held.size() ? held.substr(from) : "");
                    assert_same(expected, got);
                }
            }
        }
    }
}

TEST_CASE(
    "flash_log reader begins at an entry when its start is recycled before it reads",
    "[persistent_log]"
) {
    for (size_t chunk : {1, 7, 4096}) {
        ram_sector_store store(4, small_sector);
        auto flash = open_log(store);
        // The log starts with 380 bytes, so the second sector begins partway through a line.
        const std::string text = numbered_entries(0, 20);
        append(flash, text);
        TEST_ASSERT_EQUAL_UINT32(1, flash.head()->seq);
        TEST_ASSERT_NOT_EQUAL('\n', text[217]);
        auto [got, given] = read_across(flash, chunk, 0, [&] {
            // Fill the head sector, then start three more sectors. The last of them erases the
            // first sector.
            append(flash, std::string(42 + 2 * 218 + 1, 'x'));
            TEST_ASSERT_EQUAL_UINT32(4, flash.head()->seq);
        });
        TEST_ASSERT_EQUAL_size_t(0, given);
        assert_same(from_entry_after(text, 218), got);
    }
}

TEST_CASE(
    "flash_log reader ends its line and resumes at an entry after a sector is recycled",
    "[persistent_log]"
) {
    for (size_t chunk : {1, 2, 5, 7, 64, 200}) {
        for (size_t before : {1, 5, 7, 19, 64, 200, 218, 240}) {
            ram_sector_store store(4, small_sector);
            auto flash = open_log(store);
            // The log starts with 380 bytes, so the second sector begins partway through a line.
            const std::string text = numbered_entries(0, 20);
            append(flash, text);
            TEST_ASSERT_EQUAL_UINT32(1, flash.head()->seq);
            auto [got, given] = read_across(flash, chunk, before, [&] {
                // Fill the head sector, then start three more sectors. The last of them erases the
                // first sector.
                append(flash, std::string(42 + 2 * 218 + 1, 'x'));
                TEST_ASSERT_EQUAL_UINT32(4, flash.head()->seq);
            });
            // A reader that has finished the first sector, even if it is only at its end, loses
            // nothing.
            if (given >= 218) {
                assert_same(text, got);
                continue;
            }
            // Otherwise, the rest of the first sector is lost. The reader ends the line that it was
            // in, unless it was at the end of a line, and resumes at an entry in the next sector.
            const std::string ended = text[given - 1] == '\n' ? "" : "\n";
            assert_same(text.substr(0, given) + ended + from_entry_after(text, 218), got);
            // The line that was cut short ends there, and the next line begins an entry.
            TEST_ASSERT_EQUAL_CHAR('\n', got[given + ended.size() - 1]);
            TEST_ASSERT_EQUAL_CHAR('e', got[given + ended.size()]);
            TEST_ASSERT_TRUE(got.find("\n\n") == std::string::npos);
        }
    }
}

TEST_CASE(
    "flash_log reader checks a record partway out again before the rest",
    "[persistent_log]"
) {
    for (bool encrypted : {false, true}) {
        ram_sector_store store = make_store(encrypted, 4, line_sector);
        auto flash = open_log(store);
        append_lines(flash, 0, 20);
        const std::string full = read_all(flash);
        // After the reader has read part of the first record, a later byte in that record
        // changes. Only the CRC can detect the change, because the sector keeps its number. The
        // reader leaves out the rest of the record and of its sector, ends the line that was cut
        // short, and continues from the next entry.
        auto [got, given] = read_across(flash, 5, 5, [&] {
            store.bytes()[sector_header_size + record_header_size + 8] = std::byte{'#'};
        });
        TEST_ASSERT_EQUAL_size_t(5, given);
        assert_same(full.substr(0, 5) + "\n" + full.substr(text_per_sector + 11), got);
    }
}

TEST_CASE("flash_log reader reads on across a clear", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        ram_sector_store store = make_store(encrypted, 8, line_sector);
        auto flash = open_log(store);
        append_lines(flash, 0, 30);
        const std::string full = read_all(flash);
        auto [got, given] = read_across(flash, 7, 100, [&] {
            TEST_ASSERT_TRUE(flash.try_clear().has_value());
            append_lines(flash, 3000, 5, "newr");
        });
        assert_same(full, got);
        assert_same(lines(3000, 5, "newr"), read_all(flash));
    }
}

// =============================================================================
// Blocks, encryption and layouts
// =============================================================================

TEST_CASE("crc32 is the standard CRC-32 that FLASH_FORMAT.md names", "[persistent_log]") {
    // The check value of the standard CRC-32 (zlib's crc32()) is its CRC of these 9 bytes.
    constexpr std::string_view check = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, detail::crc32(0, std::as_bytes(std::span(check))));
}

TEST_CASE("flash_log starts each record on a block and pads it", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        ram_sector_store store = make_store(encrypted, 8, small_sector);
        auto flash = open_log(store);
        append(flash, "a\n");                  // 8 bytes, padded to 16
        append(flash, std::string(10, 'b')); // exactly 16
        append(flash, std::string(11, 'c')); // 17 bytes, padded to 32
        TEST_ASSERT_EQUAL_UINT32(sector_header_size + 16 + 16 + 32, flash.head()->offset);
        // Each record's length is at the start of a block.
        TEST_ASSERT_EQUAL_HEX8(2, std::to_integer<int>(read_block(store, sector_header_size)[0]));
        TEST_ASSERT_EQUAL_HEX8(
            10, std::to_integer<int>(read_block(store, sector_header_size + 16)[0])
        );
        TEST_ASSERT_EQUAL_HEX8(
            11, std::to_integer<int>(read_block(store, sector_header_size + 32)[0])
        );
        // The padding is written as 0xFF. It is stored as 0xFF when the store is unencrypted, and
        // encrypted when it isn't.
        auto first = read_block(store, sector_header_size);
        TEST_ASSERT_TRUE(all_erased(std::span(first).subspan(8)));
        auto last = read_block(store, sector_header_size + 48);
        TEST_ASSERT_EQUAL_HEX8('c', std::to_integer<int>(last[0]));
        TEST_ASSERT_TRUE(all_erased(std::span(last).subspan(1)));
        TEST_ASSERT_FALSE(all_erased(read_raw_block(store, sector_header_size + 48)));
        auto first_raw = read_raw_block(store, sector_header_size);
        TEST_ASSERT_EQUAL(!encrypted, all_erased(std::span(first_raw).subspan(8)));
        // After the records, the blocks are erased.
        TEST_ASSERT_TRUE(all_erased(read_raw_block(store, sector_header_size + 64)));

        const std::string text = "a\n" + std::string(10, 'b') + std::string(11, 'c');
        assert_same(text, read_all(flash));
        assert_same(text, read_all(flash, 1));
        auto reopened = open_log(store, flash.head());
        TEST_ASSERT_TRUE(reopened.opened_by_hint());
        TEST_ASSERT_TRUE(open_log(store).head() == flash.head());
        TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    }
}

TEST_CASE("flash_log splits text to fill a sector's last block", "[persistent_log]") {
    ram_sector_store store(8, sector_header_size + 48);
    auto flash = open_log(store);
    // Each sector has 48 bytes after the header. A 26-byte line takes 32 of them, which leaves 16
    // for 10 more bytes of text.
    append(flash, std::string(25, 'a') + "\n");
    append(flash, lines(0, 1));
    TEST_ASSERT_EQUAL_UINT32(1, flash.head()->sector);
    TEST_ASSERT_EQUAL_UINT32(sector_header_size + 16, flash.head()->offset);
    assert_same(std::string(25, 'a') + "\n" + lines(0, 1), read_all(flash));
    assert_same(lines(0, 1), tail_of(flash, 11));
}

TEST_CASE("flash_log ends a sector's records at a block stored erased", "[persistent_log]") {
    ram_sector_store store(ram_sector_store::encrypted, 8, small_sector);
    position hint;
    {
        auto flash = open_log(store);
        append_lines(flash, 0, 3);
        hint = *flash.head();
    }
    size_t next = hint.sector * small_sector + hint.offset;
    // The erased block after the records decrypts to noise, but as stored, it is erased.
    TEST_ASSERT_FALSE(all_erased(read_block(store, next)));
    TEST_ASSERT_TRUE(all_erased(read_raw_block(store, next)));
    TEST_ASSERT_TRUE(open_log(store, hint).opened_by_hint());
    TEST_ASSERT_TRUE(open_log(store).head() == hint);
    assert_same(lines(0, 3), read_all(open_log(store)));

    // One stored byte that isn't erased makes the block a record, which fails its CRC, so the
    // writer moves on to a new sector.
    store.bytes()[next + 9] = std::byte{0x00};
    auto flash = open_log(store, hint);
    TEST_ASSERT_FALSE(flash.opened_by_hint());
    TEST_ASSERT_EQUAL_UINT32(small_sector, flash.head()->offset);
    assert_same(lines(0, 3), read_all(flash));
    append(flash, "after\n");
    TEST_ASSERT_EQUAL_UINT32(hint.sector + 1, flash.head()->sector);
    assert_same(lines(0, 3) + "after\n", read_all(open_log(store)));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("flash_log moves on from a block a power cut tore, encrypted", "[persistent_log]") {
    // Cut the power in the record's first block, which holds its header, and in its second, which
    // holds its text.
    for (size_t cut : {size_t{5}, size_t{16 + 3}}) {
        ram_sector_store store(ram_sector_store::encrypted, 8, small_sector);
        auto flash = open_log(store);
        append_lines(flash, 0, 3);
        position hint = *flash.head();
        store.cut_after(cut);
        TEST_ASSERT_FALSE(flash.try_append(lines(10, 3)).has_value());
        store.restore_power();
        // The torn block reads as noise, so the record fails its CRC.
        const size_t at = hint.sector * small_sector + hint.offset;
        TEST_ASSERT_FALSE(all_erased(read_raw_block(store, at)));
        auto reopened = open_log(store);
        TEST_ASSERT_EQUAL_UINT32(hint.sector, reopened.head()->sector);
        TEST_ASSERT_EQUAL_UINT32(small_sector, reopened.head()->offset);
        TEST_ASSERT_FALSE(open_log(store, hint).opened_by_hint());
        assert_same(lines(0, 3), read_all(reopened));
        append(reopened, "after\n");
        assert_same(lines(0, 3) + "after\n", read_all(open_log(store)));
        TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    }
}

TEST_CASE("flash_log reads a sector of a newer layout as unused", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        ram_sector_store base = make_store(encrypted, 8, small_sector);
        position hint;
        {
            auto flash = open_log(base);
            append_lines(flash, 0, 10); // two sectors
            hint = *flash.head();
        }
        TEST_ASSERT_EQUAL_UINT32(1, hint.sector);
        TEST_ASSERT_FALSE(open_log(base).found_newer_format());

        // A sector of a later layout, numbered as if it were newer than the head, is left out, and
        // the log notes that it found a newer layout.
        ram_sector_store newer = base;
        put_sector_header(newer, 4, sector_header("PLOG", 2, 100));
        auto flash = open_log(newer);
        TEST_ASSERT_TRUE(flash.found_newer_format());
        TEST_ASSERT_TRUE(flash.head() == hint);
        assert_same(lines(0, 10), read_all(flash));
        // Opened by its hint, the log reads only the head's header and the next sector's header.
        TEST_ASSERT_FALSE(open_log(newer, hint).found_newer_format());
        put_sector_header(newer, 2, sector_header("PLOG", 0xFFFE, 100));
        auto hinted = open_log(newer, hint);
        TEST_ASSERT_TRUE(hinted.opened_by_hint());
        TEST_ASSERT_TRUE(hinted.found_newer_format());
        // Sectors of the newer layout are overwritten when the ring comes around to them.
        append(flash, lines(100, 40));
        TEST_ASSERT_TRUE(lines(0, 10) + lines(100, 40) == read_all(open_log(newer)));

        // A sector with another magic ("MLG\x01" here), or with this layout's magic at an older
        // version (0 here), is unused, and isn't noted as newer.
        for (auto h : {sector_header("MLG\x01", 0, 100), sector_header("PLOG", 0, 100)}) {
            ram_sector_store other = base;
            put_sector_header(other, 4, h);
            auto opened = open_log(other);
            TEST_ASSERT_FALSE(opened.found_newer_format());
            TEST_ASSERT_TRUE(opened.head() == hint);
        }
        // A newer version whose header fails its CRC is torn, not newer.
        auto bad = sector_header("PLOG", 2, 100);
        bad[13] ^= std::byte{0x01};
        ram_sector_store torn = base;
        put_sector_header(torn, 4, bad);
        TEST_ASSERT_FALSE(open_log(torn).found_newer_format());
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("flash_log's throwing calls throw what the try_ calls return", "[persistent_log]") {
    auto code_of = [](auto&& call) {
        try {
            call();
        } catch (const std::system_error& e) {
            return e.code();
        }
        return std::error_code();
    };

    ram_sector_store one(1, small_sector);
    TEST_ASSERT_TRUE(code_of([&] { (void)flash_log::open(one); }) == idfxx::errc::invalid_arg);

    ram_sector_store store(6, small_sector);
    auto flash = flash_log::open(store);
    flash.append("first\n");
    flash.append("second\n");
    TEST_ASSERT_TRUE("first\nsecond\n" == read_all(flash));

    auto r = flash.read();
    std::string buf(64, '\0');
    size_t n = r.next(std::span(buf));
    TEST_ASSERT_TRUE("first\nsecond\n" == buf.substr(0, n));
    TEST_ASSERT_EQUAL_size_t(0, r.next(std::span(buf)));

    std::array<char, 8> tail;
    n = flash.tail(tail);
    TEST_ASSERT_TRUE("second\n" == std::string(tail.data(), n));

    flash.clear();
    TEST_ASSERT_TRUE(read_all(flash).empty());
    flash.append("after\n");
    TEST_ASSERT_TRUE("after\n" == read_all(flash));
    flash.append("cut");
    flash.end_line();
    TEST_ASSERT_TRUE("after\ncut\n" == read_all(flash));

    store.cut_after(0);
    TEST_ASSERT_TRUE(code_of([&] { flash.append("lost\n"); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { flash.clear(); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { (void)flash.read(); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { (void)flash.tail(tail); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { flash.end_line(); }) == idfxx::errc::invalid_state);
}
#endif

TEST_CASE(
    "flash_log tail ends at the log's end past a line a torn record cut short",
    "[persistent_log]"
) {
    size_t runs = 0;
    for (size_t budget = 0; budget < 120; ++budget) {
        ram_sector_store store(8, small_sector);
        auto flash = open_log(store);
        append_lines(flash, 0, 6);
        // The append is split across sectors partway through a line, and the power cut tears the
        // record in the next sector.
        store.cut_after(budget);
        const bool appended = flash.try_append(lines(6, 4)).has_value();
        store.restore_power();
        if (appended) {
            break;
        }
        auto reopened = open_log(store);
        TEST_ASSERT_TRUE(reopened.try_end_line().has_value());
        append(reopened, lines(10, 1));
        const std::string all = read_all(reopened);
        TEST_ASSERT_TRUE(all.ends_with(lines(10, 1)));

        // Every tail holds the log's newest complete lines, as many as fit.
        for (size_t want = 1; want <= all.size() + 2; ++want) {
            const std::string t = tail_of(reopened, want);
            const bool ends = all.ends_with(t) && (t.empty() || t.back() == '\n');
            const bool begins =
                t.size() == all.size() || (!t.empty() && all[all.size() - t.size() - 1] == '\n');
            const bool fills = want >= all.size() ? t == all : t.size() + lines(0, 1).size() > want;
            if (!(ends && (begins || t.empty()) && fills)) {
                std::printf(
                    "budget %u tail %u: '%s' of '%s'\n",
                    unsigned(budget),
                    unsigned(want),
                    t.c_str(),
                    all.c_str()
                );
            }
            TEST_ASSERT_TRUE(ends && (begins || t.empty()) && fills);
        }
        ++runs;
    }
    TEST_ASSERT_GREATER_THAN_size_t(10, runs);
}

TEST_CASE(
    "flash_log read keeps the entry after a torn record when it has given nothing yet",
    "[persistent_log]"
) {
    size_t runs = 0;
    for (size_t budget = 0; budget < 400; ++budget) {
        ram_sector_store store(4, 128);
        {
            auto flash = open_log(store);
            append(flash, "e00000\n");
            // Append a line that spans three sectors, and cut the power during the append.
            store.cut_after(budget);
            const bool appended = flash.try_append("L" + std::string(248, 'x') + "\n").has_value();
            store.restore_power();
            if (appended) {
                break;
            }
        }
        auto flash = open_log(store);
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        // Once the line's first sector is gone, a read begins by seeking through the rest of the
        // line, and the torn record ends that sector. The reader treats what comes next as the
        // start of a line, as the tail does.
        for (int i = 1; i <= 8; ++i) {
            char line[16];
            std::snprintf(line, sizeof line, "e%05d\n", i);
            append(flash, line);
            const std::string all = read_all(flash);
            const std::string tail = tail_of(flash, 4096);
            if (all != tail) {
                std::printf(
                    "budget %u after e%05d: read '%s', tail '%s'\n",
                    unsigned(budget),
                    i,
                    all.c_str(),
                    tail.c_str()
                );
            }
            TEST_ASSERT_TRUE(all == tail);
        }
        ++runs;
    }
    TEST_ASSERT_GREATER_THAN_size_t(50, runs);
}

// =============================================================================
// Ending a line left open by a failed append, and reading the newest entries
// =============================================================================

namespace {

using testing::last_of;
using testing::split_entries;

// The stored text of each entry that an entry buffer takes from `r`.
std::vector<std::string> buffered_entries(flash_log::reader r) {
    entry_buffer b;
    std::vector<std::string> out;
    entry e;
    while (true) {
        auto more = b.next(e, [&r](std::span<char> room) { return r.try_next(room); });
        TEST_ASSERT_TRUE(more.has_value());
        if (!*more) {
            return out;
        }
        out.emplace_back(detail::entry_access::stored(e));
    }
}

// Checks that the text that `log` reads, and the entries taken from it, show the entry that `cut`
// ends with as its own entry, ended where it was cut, followed by the entry appended after it.
void assert_ended(const flash_log& flash, const std::string& before, const std::string& cut) {
    const std::string after = "e00099\n\tmore\n";
    assert_same(before + cut + "\n" + after, read_all(flash));
    auto r = flash.try_read();
    TEST_ASSERT_TRUE(r.has_value());
    const auto got = buffered_entries(*r);
    TEST_ASSERT_TRUE(got == split_entries(before + cut + "\n" + after));
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(2, got.size());
    const size_t last = cut.rfind("\ne");
    assert_same(last == std::string::npos ? cut : cut.substr(last + 1), got[got.size() - 2]);
    assert_same("e00099\n\tmore", got.back());
}

} // namespace

TEST_CASE(
    "flash_log ends a line that a failed append left open, before the next append",
    "[persistent_log]"
) {
    for (bool encrypted : {false, true}) {
        // The append's text runs past the end of the head sector, partway through a line. The
        // power is cut after the next sector's header is written, so the record that holds the
        // rest of the text is not written at all: its first block is erased.
        ram_sector_store store = make_store(encrypted, 8, small_sector);
        auto flash = open_log(store);
        append(flash, "e00000\n");
        const size_t room = small_sector - flash.head()->offset - record_header_size;
        const std::string text = numbered_entries(1, 20);
        TEST_ASSERT_TRUE(text[room - 1] != '\n');
        store.cut_after(small_sector - flash.head()->offset + block_size);
        TEST_ASSERT_FALSE(flash.try_append(text).has_value());
        store.restore_power();
        // The log goes on without being opened again.
        append(flash, "e00099\n\tmore\n");
        assert_ended(flash, "e00000\n", text.substr(0, room));
        assert_ended(open_log(store), "e00000\n", text.substr(0, room));
        TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    }
}

TEST_CASE(
    "flash_log ends a line left open when starting a sector failed, before the next append",
    "[persistent_log]"
) {
    // The text fills the head sector exactly, ending partway through a line. Starting the next
    // sector fails: the power is cut during its erase, or while its header is written.
    const std::string text = numbered_entries(0, 11) + "e00011 cu";
    for (size_t budget : {size_t{0}, size_t{5}}) {
        ram_sector_store store(8, small_sector);
        auto flash = open_log(store);
        append(flash, text);
        TEST_ASSERT_EQUAL_UINT32(small_sector, flash.head()->offset);
        store.cut_after(budget);
        TEST_ASSERT_FALSE(flash.try_append("e00012\n").has_value());
        store.restore_power();
        // The log goes on without being opened again.
        append(flash, "e00099\n\tmore\n");
        assert_ended(flash, numbered_entries(0, 11), "e00011 cu");
        assert_ended(open_log(store), numbered_entries(0, 11), "e00011 cu");
    }
}

TEST_CASE("flash_log forgets a line left open once it is cleared", "[persistent_log]") {
    ram_sector_store store(8, small_sector);
    auto flash = open_log(store);
    append(flash, numbered_entries(0, 11) + "e00011 cu");
    store.cut_after(0);
    TEST_ASSERT_FALSE(flash.try_append("e00012\n").has_value());
    store.restore_power();
    TEST_ASSERT_TRUE(flash.try_clear().has_value());
    append(flash, "e00099\n");
    assert_same("e00099\n", read_all(flash));
}

TEST_CASE(
    "flash_log read_newest returns the newest entries, for any count and scratch size",
    "[persistent_log]"
) {
    size_t runs = 0;
    for (size_t sector_size : {size_t{80}, size_t{128}, size_t{256}, size_t{1024}}) {
        // A wrapped ring, written in appends of several sizes, so that records split across
        // sectors at different places.
        ram_sector_store store(6, sector_size);
        auto flash = open_log(store);
        for (int i = 0, n = 1; i < 400; i += n, n = n % 7 + 1) {
            append(flash, numbered_entries(i, n));
        }
        const auto all = split_entries(read_all(flash));
        TEST_ASSERT_GREATER_THAN_size_t(4, all.size());
        // With `largest` at the size of one entry, the reader of a small count reads only part
        // of the log. With a scratch buffer of a few bytes, it reads the log in many chunks, so
        // the first entry to return lands in the first, a middle or the last chunk.
        for (size_t largest : {size_t{19}, size_t{457}}) {
            for (size_t scratch_size : {size_t{5}, size_t{64}, size_t{300}}) {
                std::string scratch(scratch_size, '\0');
                const size_t size = all.size();
                for (size_t count : {size_t{0}, size_t{1}, size_t{2}, size_t{3}, size / 3, size / 2,
                                     size - 2, size - 1, size, size + 1, size + 2}) {
                    auto r = flash.try_read_newest(count, largest, std::span(scratch));
                    TEST_ASSERT_TRUE(r.has_value());
                    TEST_ASSERT_TRUE(buffered_entries(*r) == last_of(all, count));
                    ++runs;
                }
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_size_t(100, runs);
}

TEST_CASE(
    "flash_log read_newest wants all of the log for a count past the size_t limit",
    "[persistent_log]"
) {
    ram_sector_store store(6, small_sector);
    auto flash = open_log(store);
    append(flash, numbered_entries(0, 100));
    const auto all = split_entries(read_all(flash));
    std::string scratch(64, '\0');
    for (size_t largest : {size_t{1}, size_t{457}, size_t{1000}}) {
        // At SIZE_MAX / largest - 1, the bytes wanted fit in a size_t. From SIZE_MAX / largest on,
        // they would overflow, so the bytes wanted are SIZE_MAX.
        for (size_t count : {SIZE_MAX / largest - 1, SIZE_MAX / largest, SIZE_MAX}) {
            auto r = flash.try_read_newest(count, largest, std::span(scratch));
            TEST_ASSERT_TRUE(r.has_value());
            TEST_ASSERT_TRUE(buffered_entries(*r) == all);
        }
    }
}

TEST_CASE(
    "flash_log read_newest returns a reader that checks each record on each call",
    "[persistent_log]"
) {
    // The reader that read_newest returns may be used while the log changes, so each call checks
    // a partly returned record against its CRC again, as a reader from read does. Here a byte of
    // the record that the reader is in is damaged between two calls, after the bytes returned. The
    // reader then ends the line and reads nothing more of the sector. With a count of 1, the reader
    // begins at the second entry. With a count of 5, it begins at the first.
    for (size_t count : {size_t{1}, size_t{5}}) {
        ram_sector_store store(8, 1024);
        auto flash = open_log(store);
        const std::string first = "first entry\n";
        const std::string second = "second " + std::string(100, 's') + "\n";
        append(flash, first);
        append(flash, second);
        std::array<char, 64> scratch;
        auto r = flash.try_read_newest(count, 200, scratch);
        TEST_ASSERT_TRUE(r.has_value());
        std::array<char, 10> piece;
        auto n = r->try_next(piece);
        TEST_ASSERT_TRUE(n.has_value());
        std::string got(piece.data(), *n);
        // The first record starts after the sector's header, and the second one block later.
        const size_t record_at = sector_header_size + (count == 1 ? 32 : 0);
        store.bytes()[record_at + record_header_size + 11] ^= std::byte{0x01};
        while (true) {
            n = r->try_next(piece);
            TEST_ASSERT_TRUE(n.has_value());
            if (*n == 0) {
                break;
            }
            got.append(piece.data(), *n);
        }
        assert_same((count == 1 ? second : first).substr(0, 10) + "\n", got);
    }
}

// =============================================================================
// Bytes read
// =============================================================================

TEST_CASE("flash_log open and end_line read the head sector once", "[persistent_log]") {
    for (bool hinted : {false, true}) {
        // The head sector holds most of a sector of text, and the log ends partway through a
        // line. The open checks the head sector's records and erased bytes, about 4.1 KiB, and
        // end_line then knows from that check how the log ends. A second check of the records
        // would read about 3.4 KiB more.
        testing::counting_store store(8, 4096);
        position hint;
        {
            auto flash = open_log(store);
            append(flash, lines(0, 300));
            append(flash, "open line");
            hint = *flash.head();
        }
        store.reset_count();
        auto flash = hinted ? open_log(store, hint) : open_log(store);
        TEST_ASSERT_EQUAL(hinted, flash.opened_by_hint());
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        const size_t read = store.bytes_read();
        std::printf(
            "flash_log open %s and end_line read %u bytes\n",
            hinted ? "by hint" : "by scan",
            unsigned(read)
        );
        TEST_ASSERT_LESS_OR_EQUAL_size_t(4608, read);
        assert_same(lines(0, 300) + "open line\n", read_all(flash));
        // Once end_line has appended the newline, it appends nothing more.
        TEST_ASSERT_TRUE(flash.try_end_line().has_value());
        assert_same(lines(0, 300) + "open line\n", read_all(flash));
    }
}

TEST_CASE(
    "flash_log checks each record once in the passes that find the end of the log",
    "[persistent_log]"
) {
    // The first sector holds one record of 4048 bytes, and the head sector after it holds 110
    // bytes. The passes read the text in pieces much smaller than a record. Each pass checks each
    // record's CRC once, and then reads its text, so each pass reads about 8.3 KiB. A pass that
    // checked the record again for each piece would read more than 100 KiB.
    testing::counting_store store(8, 4096);
    auto flash = open_log(store);
    append(flash, lines(0, 368));
    append(flash, lines(368, 10));
    TEST_ASSERT_EQUAL_UINT32(1, flash.head()->sector);

    store.reset_count();
    std::optional<flash_log::tail_reader> t;
    TEST_ASSERT_TRUE(flash.try_read_tail(150, t).has_value());
    const size_t tail = store.bytes_read();
    TEST_ASSERT_EQUAL_size_t(143, t->size());

    store.reset_count();
    std::array<char, 128> scratch;
    auto r = flash.try_read_newest(3, 100, scratch);
    TEST_ASSERT_TRUE(r.has_value());
    const size_t newest = store.bytes_read();
    std::printf(
        "flash_log read_tail made its reader in %u bytes, read_newest in %u bytes\n",
        unsigned(tail),
        unsigned(newest)
    );
    TEST_ASSERT_LESS_OR_EQUAL_size_t(24576, tail);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(24576, newest);
    TEST_ASSERT_TRUE(buffered_entries(*r) == split_entries(lines(375, 3)));
}

TEST_CASE(
    "flash_log tail reader starts where the search for its first entry stopped",
    "[persistent_log]"
) {
    // As in the test above, the tail's 143 bytes begin 4015 bytes into the log. The tail reader
    // starts at the last piece that the search for the tail read, so it reads less than a piece
    // before the tail. Then it checks the head sector's record and reads the tail. A reader that
    // started at the log's start would read past 4015 bytes again.
    testing::counting_store store(8, 4096);
    auto flash = open_log(store);
    append(flash, lines(0, 368));
    append(flash, lines(368, 10));
    std::optional<flash_log::tail_reader> t;
    TEST_ASSERT_TRUE(flash.try_read_tail(150, t).has_value());
    store.reset_count();
    std::string got;
    std::array<char, 256> piece;
    while (true) {
        auto n = t->try_next(piece);
        TEST_ASSERT_TRUE(n.has_value());
        if (*n == 0) {
            break;
        }
        got.append(piece.data(), *n);
    }
    const size_t read = store.bytes_read();
    std::printf("flash_log tail reader read %u bytes\n", unsigned(read));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(512, read);
    assert_same(lines(365, 13), got);
}
