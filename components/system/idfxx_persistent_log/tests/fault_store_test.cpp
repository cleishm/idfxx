// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_persistent_log's fault store, on a RAM sector store that follows the rules
// of NOR flash. Small sectors and slots make the ring wrap after a few entries.

#include "../src/fault_store.hpp"
#include "../src/sector_store.hpp"
#include "esp_rom_crc.h"
#include "unity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace idfxx::persistent_log;
using namespace std::chrono_literals;

namespace {

// Each slot is eight 128-byte sectors, with 512 bytes for context after the header. The store
// holds four slots.
constexpr size_t small_sector = 128;
constexpr size_t small_sectors = 32;
constexpr size_t small_slot = 1024;
constexpr size_t small_room = small_slot - fault_header_size;
constexpr size_t header_sectors = fault_header_size / small_sector;

ram_sector_store make_store(bool encrypted, size_t sectors, size_t sector_size) {
    return encrypted ? ram_sector_store(ram_sector_store::encrypted, sectors, sector_size)
                     : ram_sector_store(sectors, sector_size);
}

// A store that passes each call on to another store, and asks for writes of 32 bytes, which is
// more than a block.
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

void put_u32(std::span<std::byte> h, size_t at, uint32_t v) {
    for (size_t i = 0; i < 4; ++i) {
        h[at + i] = std::byte((v >> (8 * i)) & 0xFF);
    }
}

// Returns a slot header with the given magic and layout version, and with a good CRC.
std::array<std::byte, fault_header_size>
slot_header(const char (&magic)[5], uint16_t version, uint32_t seq) {
    std::array<std::byte, fault_header_size> h{};
    for (size_t i = 0; i < 4; ++i) {
        h[i] = std::byte(magic[i]);
    }
    h[4] = std::byte(version & 0xFF);
    h[5] = std::byte(version >> 8);
    put_u32(h, 8, seq);
    for (size_t i = 0; i < 4; ++i) {
        h[44 + i] = std::byte("xxxx"[i]); // a kind, as a v1 header would have
    }
    uint32_t crc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(h.data()), 508);
    put_u32(h, 508, crc);
    return h;
}

// Writes a header at the start of a slot, the way the store writes a header, whatever the header
// holds.
void put_slot_header(sector_store& store, size_t slot, std::span<const std::byte> h) {
    size_t first = slot * small_slot / small_sector;
    for (size_t i = 0; i < header_sectors; ++i) {
        TEST_ASSERT_TRUE(store.try_erase_sector(first + i).has_value());
    }
    TEST_ASSERT_TRUE(store.try_write(slot * small_slot, h).has_value());
}

fault_store open_store(sector_store& store, size_t slot_size = small_slot) {
    auto faults = fault_store::try_open(store, slot_size);
    TEST_ASSERT_TRUE(faults.has_value());
    return std::move(*faults);
}

fault_input make_fault(std::string_view kind, std::string_view detail = "detail") {
    return fault_input{
        .kind = kind,
        .detail = detail,
        .version = "1.2.3",
        .boot = 4,
        .wake = 17,
        .wall = std::chrono::sys_days{std::chrono::November / 20 / 2026} + 10h + 31min + 2s,
        .uptime = 1234ms,
    };
}

uint32_t record(
    fault_store& faults,
    const fault_input& f,
    std::string_view a = {},
    std::string_view b = {}
) {
    auto seq = faults.try_record(f, a, b);
    TEST_ASSERT_TRUE(seq.has_value());
    return *seq;
}

void clear(fault_store& faults) {
    TEST_ASSERT_TRUE(faults.try_clear().has_value());
}

size_t count(const fault_store& faults) {
    auto n = faults.try_count();
    TEST_ASSERT_TRUE(n.has_value());
    return *n;
}

// Returns a fault's line as std::format writes it. Without std::format, it calls the code that
// writes the line for std::format.
std::string header_line(const fault& e) {
#ifdef CONFIG_IDFXX_STD_FORMAT
    std::string line = std::format("{}", e);
#else
    std::string line;
    detail::format_fault(
        e,
        [](void* sink, std::string_view piece) { static_cast<std::string*>(sink)->append(piece); },
        &line
    );
#endif
    TEST_ASSERT_LESS_OR_EQUAL_size_t(max_fault_line_size, line.size());
    return line;
}

// The rest of the current entry's context, read in pieces of `chunk` bytes.
std::string read_context(fault_store::reader& r, size_t chunk = 256) {
    std::string context;
    std::vector<char> buf(chunk);
    for (;;) {
        auto n = r.try_next_context(std::span(buf));
        TEST_ASSERT_TRUE(n.has_value());
        TEST_ASSERT_LESS_OR_EQUAL_size_t(chunk, *n);
        if (*n == 0) {
            return context;
        }
        context.append(buf.data(), *n);
    }
}

struct listed {
    fault entry;
    std::string line;
    std::string context;
};

// The next entry from a reader, if there is one.
std::optional<listed> next_listed(fault_store::reader& r) {
    listed l;
    auto more = r.try_next(l.entry);
    TEST_ASSERT_TRUE(more.has_value());
    if (!*more) {
        return std::nullopt;
    }
    l.line = header_line(l.entry);
    l.context = read_context(r);
    TEST_ASSERT_EQUAL_size_t(l.entry.context_size(), l.context.size());
    return l;
}

std::vector<listed> list_all(const fault_store& faults) {
    std::vector<listed> out;
    auto r = faults.read();
    while (auto l = next_listed(r)) {
        out.push_back(std::move(*l));
    }
    return out;
}

std::vector<uint32_t> seqs(const std::vector<listed>& entries) {
    std::vector<uint32_t> out;
    for (const auto& e : entries) {
        out.push_back(e.entry.sequence());
    }
    return out;
}

void assert_seqs(const std::vector<uint32_t>& expected, const fault_store& faults) {
    auto got = seqs(list_all(faults));
    TEST_ASSERT_EQUAL_size_t(expected.size(), got.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], got[i]);
    }
    TEST_ASSERT_EQUAL_size_t(expected.size(), count(faults));
}

void assert_same(std::string_view expected, const std::string& actual) {
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    TEST_ASSERT_TRUE(expected == actual);
}

// Lines of 11 bytes each, such as "line 00042\n".
std::string lines(int from, int count, const char* word = "line") {
    std::string s;
    char line[32];
    for (int i = from; i < from + count; ++i) {
        int n = std::snprintf(line, sizeof line, "%s %05d\n", word, i);
        s.append(line, static_cast<size_t>(n));
    }
    return s;
}

} // namespace

// =============================================================================
// Opening, recording and reading
// =============================================================================

TEST_CASE("fault_store opens an erased store empty", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    TEST_ASSERT_EQUAL_size_t(4, faults.slots());
    TEST_ASSERT_EQUAL_size_t(small_room, faults.room());
    TEST_ASSERT_EQUAL_size_t(0, count(faults));
    TEST_ASSERT_EQUAL_size_t(0, list_all(faults).size());
}

TEST_CASE("fault_store refuses a slot size that doesn't fit the store", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    TEST_ASSERT_FALSE(fault_store::try_open(store, small_slot + 1).has_value());
    // A slot smaller than the header, and a slot that holds only the header.
    TEST_ASSERT_FALSE(fault_store::try_open(store, small_sector).has_value());
    TEST_ASSERT_FALSE(fault_store::try_open(store, fault_header_size).has_value());
    TEST_ASSERT_FALSE(fault_store::try_open(store, 2 * small_sectors * small_sector).has_value());
    TEST_ASSERT_TRUE(fault_store::try_open(store, small_sectors * small_sector).has_value());
    TEST_ASSERT_TRUE(fault_store::try_open(store, 5 * small_sector).has_value());
}

TEST_CASE("fault_store refuses a store it can't write in blocks", "[persistent_log]") {
    // A store whose sectors aren't a whole number of blocks, and a store that writes more than a
    // block at a time.
    ram_sector_store odd(16, 72);
    auto r = fault_store::try_open(odd, 8 * 72);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    ram_sector_store inner(small_sectors, small_sector);
    wide_store wide(inner);
    r = fault_store::try_open(wide, small_slot);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    ram_sector_store enc(ram_sector_store::encrypted, small_sectors, small_sector);
    TEST_ASSERT_TRUE(fault_store::try_open(enc, small_slot).has_value());
}

TEST_CASE("fault_store fails to open when a read fails", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    store.cut_after(0);
    TEST_ASSERT_FALSE(store.try_write(0, std::as_bytes(std::span("x", 1))).has_value());
    TEST_ASSERT_FALSE(fault_store::try_open(store, small_slot).has_value());
}

TEST_CASE("fault_store reads back every field, with 4 KiB sectors", "[persistent_log]") {
    ram_sector_store store(6, 4096);
    auto faults = open_store(store, default_fault_slot_size);
    TEST_ASSERT_EQUAL_size_t(3, faults.slots());
    TEST_ASSERT_EQUAL_size_t(default_fault_slot_size - fault_header_size, faults.room());
    std::string context = lines(0, 600); // 6600 bytes, which reach into the slot's second sector
    fault_input f = make_fault("panic", "LoadProhibited pc=0x42001234");
    f.wall = f.wall.value() + 999ms; // the store keeps only whole seconds
    f.elf_sha256 = {
        std::byte{0x3a}, std::byte{0x5f}, std::byte{0x09}, std::byte{0xc2},
        std::byte{0xe1}, std::byte{0xb0}, std::byte{0x4d}, std::byte{0x77},
    };
    f.task = "main";
    f.exception_pc = 0x42001234;
    const std::array<uint32_t, 3> frames = {0x42001234, 0x42005678, 0x40380abc};
    f.frames = frames;
    TEST_ASSERT_EQUAL_UINT32(0, record(faults, f, context));

    auto check = [&](const fault_store& from) {
        auto got = list_all(from);
        TEST_ASSERT_EQUAL_size_t(1, got.size());
        const auto& e = got[0].entry;
        TEST_ASSERT_EQUAL_UINT32(0, e.sequence());
        TEST_ASSERT_TRUE(e.kind() == "panic");
        TEST_ASSERT_TRUE(e.detail() == "LoadProhibited pc=0x42001234");
        TEST_ASSERT_TRUE(e.version() == "1.2.3");
        TEST_ASSERT_EQUAL_UINT32(4, e.boot());
        TEST_ASSERT_EQUAL_UINT32(17, e.wake());
        TEST_ASSERT_TRUE(e.wall_time().has_value());
        TEST_ASSERT_TRUE(
            *e.wall_time() ==
            std::chrono::sys_days{std::chrono::November / 20 / 2026} + 10h + 31min + 2s
        );
        TEST_ASSERT_TRUE(e.uptime() == 1234ms);
        TEST_ASSERT_EQUAL_UINT32(context.size(), e.context_size());
        TEST_ASSERT_FALSE(e.damaged());
        TEST_ASSERT_TRUE(std::ranges::equal(f.elf_sha256, e.elf_sha256()));
        TEST_ASSERT_TRUE(e.task() == "main");
        TEST_ASSERT_EQUAL_HEX32(0x42001234, e.exception_pc());
        TEST_ASSERT_EQUAL_UINT32(3, e.frames().size());
        TEST_ASSERT_TRUE(std::ranges::equal(frames, e.frames()));
        assert_same(context, got[0].context);
        assert_same(
            "fault seq=0 kind=panic boot=4 wake=17 utc=2026-11-20T10:31:02Z uptime=1234 "
            "version=\"1.2.3\" detail=\"LoadProhibited pc=0x42001234\" elf=3a5f09c2e1b04d77 "
            "task=\"main\" pc=0x42001234 frames=0x42001234,0x42005678,0x40380abc context=6600",
            got[0].line
        );
        TEST_ASSERT_EQUAL_size_t(1, count(from));
    };
    check(faults);
    check(open_store(store, default_fault_slot_size));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("fault_store keeps an entry with no wall-clock time or context", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    fault_input f = make_fault("brownout", "");
    f.wall = std::nullopt;
    f.version = "";
    f.uptime = 0ms;
    record(faults, f);
    auto got = list_all(open_store(store));
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    TEST_ASSERT_FALSE(got[0].entry.wall_time().has_value());
    assert_same("", got[0].context);
    // When the fault has no backtrace fields, they read as zero and empty, and no frames are
    // written.
    TEST_ASSERT_TRUE(got[0].entry.task().empty());
    TEST_ASSERT_EQUAL_UINT32(0, got[0].entry.exception_pc());
    TEST_ASSERT_EQUAL_UINT32(0, got[0].entry.frames().size());
    assert_same(
        "fault seq=0 kind=brownout boot=4 wake=17 utc=none uptime=0 version=\"\" detail=\"\" "
        "elf=0000000000000000 task=\"\" pc=0x00000000 context=0",
        got[0].line
    );
    // Recording an entry erases only the sectors that it needs. Here, those are the four sectors
    // that hold the header.
    TEST_ASSERT_EQUAL_size_t(header_sectors, store.erases());
}

TEST_CASE("fault_store keeps the uptime within its field", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    fault_input f = make_fault("task_wdt");
    f.uptime = -5ms;
    record(faults, f);
    f.uptime = std::chrono::milliseconds(int64_t{1} << 40);
    record(faults, f);
    auto got = list_all(faults);
    TEST_ASSERT_TRUE(got[0].entry.uptime() == 0ms);
    TEST_ASSERT_TRUE(got[1].entry.uptime() == std::chrono::milliseconds(0xFFFFFFFF));
}

TEST_CASE("fault_store cuts the version and detail to fit their fields", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    std::string detail(200, 'd');
    std::string version = std::string(30, 'v') + "\xC3\xA9"; // ends with a 2-byte character
    fault_input f = make_fault("panic", detail);
    f.version = version;
    f.task = "a_task_name_too_long";
    std::array<uint32_t, max_fault_frames + 4> frames;
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i] = 0x42000000 + static_cast<uint32_t>(i);
    }
    f.frames = frames;
    record(faults, f);
    auto got = list_all(faults);
    assert_same(std::string(fault_detail_size - 1, 'd'), std::string(got[0].entry.detail()));
    // Keeping 31 bytes would split the last character, so the complete character is left out.
    assert_same(std::string(30, 'v'), std::string(got[0].entry.version()));
    assert_same("a_task_name_too", std::string(got[0].entry.task()));
    // The innermost frames are kept.
    TEST_ASSERT_EQUAL_UINT32(max_fault_frames, got[0].entry.frames().size());
    TEST_ASSERT_TRUE(
        std::ranges::equal(std::span(frames).first(max_fault_frames), got[0].entry.frames())
    );
}

TEST_CASE("fault_store refuses a kind it can't print or that is reserved", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    for (std::string_view kind :
         {std::string_view(""),
          std::string_view("with space"),
          std::string_view("new\nline"),
          std::string_view("123456789012345678901234"),
          std::string_view("caf\xC3\xA9"),
          cleared_kind}) {
        auto r = faults.try_record(make_fault(kind));
        TEST_ASSERT_FALSE(r.has_value());
        TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    }
    TEST_ASSERT_EQUAL_size_t(0, store.erases());
    record(faults, make_fault("12345678901234567890123"));
    TEST_ASSERT_EQUAL_size_t(1, count(faults));
}

// =============================================================================
// Context
// =============================================================================

TEST_CASE("fault_store keeps the newest context from the start of a line", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);

    // The context is 660 bytes and the room is 512. The newest 512 bytes begin 5 bytes into line
    // 13, so line 13 is dropped too.
    record(faults, make_fault("a"), lines(0, 60));
    // The byte before the newest 512 bytes ends a line, so nothing more is dropped.
    std::string body = std::string(small_room - 1, 'b') + "\n";
    record(faults, make_fault("b"), "older line\n" + body);
    // A context exactly the size of the room is kept complete.
    record(faults, make_fault("c"), body);
    // No line starts within the newest 512 bytes, so nothing is kept.
    record(faults, make_fault("d"), std::string(600, 'z'));

    auto got = list_all(faults);
    TEST_ASSERT_EQUAL_size_t(4, got.size());
    assert_same(lines(14, 46), got[0].context);
    assert_same(body, got[1].context);
    assert_same(body, got[2].context);
    assert_same("", got[3].context);
    TEST_ASSERT_FALSE(got[3].entry.damaged());
}

TEST_CASE("fault_store keeps the newest context from the start of an entry", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    // The newest log entry in the context, padded so that the context is `over` bytes larger than
    // the room.
    const auto newest = [](size_t before, size_t over) {
        return "3 kept\n\t" + std::string(small_room + over - before - 9, 'k') + "\n";
    };

    // The newest bytes begin on a line that continues an entry. That line is dropped, and so is the
    // entry's next line.
    std::string_view on_tab = "1 a\n\tb\n\tc\n";
    record(faults, make_fault("on_tab"), std::string(on_tab) + newest(on_tab.size(), 4));
    // The newest bytes begin in the middle of an entry's first line, so the rest of that entry is
    // dropped too.
    std::string_view in_line = "2 cut here\n\tb\n\tc\n";
    record(faults, make_fault("in_line"), std::string(in_line) + newest(in_line.size(), 5));
    // Only the continuation lines of an entry fit, so nothing is kept.
    std::string tabs = "1 a\n\tbbbbbbbbbb\n\t" + std::string(small_room - 13, 'c') + "\n";
    record(faults, make_fault("tabs"), tabs);

    auto got = list_all(faults);
    TEST_ASSERT_EQUAL_size_t(3, got.size());
    assert_same(newest(on_tab.size(), 4), got[0].context);
    assert_same(newest(in_line.size(), 5), got[1].context);
    assert_same("", got[2].context);
}

TEST_CASE("fault_store takes the context in two parts", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    std::string text = lines(0, 60); // 660 bytes

    // A context short enough to keep complete, split into two parts in the middle of a line.
    std::string short_text = text.substr(0, 100);
    record(faults, make_fault("whole"), short_text.substr(0, 37), short_text.substr(37));
    // When the cut falls within the first part, the rest of the line that it falls in is dropped.
    // The context then continues with the rest of the first part and all of the second.
    record(
        faults,
        make_fault("cut_first"),
        std::string_view(text).substr(0, 200),
        std::string_view(text).substr(200)
    );
    // When the cut falls within the second part, the first part is dropped entirely.
    record(
        faults,
        make_fault("cut_second"),
        std::string_view(text).substr(0, 50),
        std::string_view(text).substr(50)
    );
    // A context with only the second part.
    record(faults, make_fault("second_only"), {}, short_text);

    auto got = list_all(faults);
    TEST_ASSERT_EQUAL_size_t(4, got.size());
    assert_same(short_text, got[0].context);
    assert_same(lines(14, 46), got[1].context);
    assert_same(lines(14, 46), got[2].context);
    assert_same(short_text, got[3].context);
}

namespace {

// A context that gives the store a string one piece at a time. A fill fails if it would go past
// `fail_at` bytes.
class string_context final : public fault_context {
public:
    explicit string_context(std::string text, size_t fail_at = std::string::npos)
        : _text(std::move(text))
        , _fail_at(fail_at) {}

    [[nodiscard]] size_t size() const override { return _text.size(); }

    [[nodiscard]] idfxx::result<void> fill(std::span<char> out) override {
        TEST_ASSERT_LESS_OR_EQUAL_size_t(256, out.size());
        TEST_ASSERT_LESS_OR_EQUAL_size_t(_text.size(), _at + out.size());
        if (_at + out.size() > _fail_at) {
            return idfxx::error(idfxx::errc::fail);
        }
        std::copy_n(_text.data() + _at, out.size(), out.data());
        _at += out.size();
        ++pieces;
        return {};
    }

    size_t pieces = 0;

private:
    std::string _text;
    size_t _fail_at;
    size_t _at = 0;
};

} // namespace

TEST_CASE("fault_store records a context given in pieces", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    // A context that fills the room exactly is kept as it is, without a cut to the start of an
    // entry.
    const std::string text = std::string(small_room - 1, 'x') + "\n";
    string_context source(text);
    TEST_ASSERT_EQUAL_UINT32(0, faults.try_record(make_fault("pieces"), source).value_or(99));
    TEST_ASSERT_EQUAL_size_t(2, source.pieces);
    auto got = list_all(faults);
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    assert_same(text, got[0].context);
    TEST_ASSERT_FALSE(got[0].entry.damaged());

    // A context larger than the room is refused before anything is erased.
    const size_t erases = store.erases();
    string_context big(text + "y\n");
    auto refused = faults.try_record(make_fault("big"), big);
    TEST_ASSERT_TRUE(!refused.has_value() && refused.error() == idfxx::errc::invalid_arg);
    string_context bad_kind(text);
    refused = faults.try_record(make_fault(cleared_kind), bad_kind);
    TEST_ASSERT_TRUE(!refused.has_value() && refused.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_EQUAL_size_t(erases, store.erases());

    // When the context fails, the record fails too, and the slot reads as empty.
    string_context failing(text, 300);
    auto failed = faults.try_record(make_fault("failing"), failing);
    TEST_ASSERT_TRUE(!failed.has_value() && failed.error() == idfxx::errc::fail);
    assert_seqs({0}, faults);
    assert_seqs({0}, open_store(store));
}

TEST_CASE("fault_store tells a recordable kind and keeps a detail's start", "[persistent_log]") {
    TEST_ASSERT_TRUE(recordable_kind("panic"));
    TEST_ASSERT_TRUE(recordable_kind("12345678901234567890123"));
    TEST_ASSERT_FALSE(recordable_kind(""));
    TEST_ASSERT_FALSE(recordable_kind("123456789012345678901234"));
    TEST_ASSERT_FALSE(recordable_kind("a b"));
    TEST_ASSERT_FALSE(recordable_kind("caf\xC3\xA9"));
    TEST_ASSERT_FALSE(recordable_kind(cleared_kind));
    TEST_ASSERT_TRUE(kept_detail("short") == "short");
    const std::string long_detail(200, 'd');
    TEST_ASSERT_TRUE(kept_detail(long_detail) == std::string_view(long_detail).substr(0, 159));
    // The detail is never shortened in the middle of a character. A 2-byte character at bytes 158
    // and 159 is left out completely.
    const std::string split = std::string(158, 'd') + "\xC3\xA9" + "more";
    TEST_ASSERT_TRUE(kept_detail(split) == std::string_view(split).substr(0, 158));
}

TEST_CASE("fault_store reads each entry with its whole context", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("a"), lines(0, 20));
    record(faults, make_fault("b"), lines(100, 3));
    auto got = list_all(faults);
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    assert_same(lines(0, 20), got[0].context);
    assert_same(lines(100, 3), got[1].context);
}

TEST_CASE("fault_store streams the context in pieces of any size", "[persistent_log]") {
    // Reads at every offset and of every length, including through the blocks of an encrypted
    // store.
    for (bool encrypted : {false, true}) {
        ram_sector_store store = make_store(encrypted, small_sectors, small_sector);
        auto faults = open_store(store);
        const std::string a = lines(0, 20); // 220 bytes
        // A context that fills the whole room.
        const std::string b = std::string(small_room - 1, 'b') + "\n";
        record(faults, make_fault("a"), a);
        record(faults, make_fault("b"), b);
        record(faults, make_fault("empty"));
        for (size_t chunk :
             {size_t{1}, size_t{7}, size_t{13}, size_t{255}, size_t{256}, size_t{1000}}) {
            auto r = faults.read();
            fault e;
            TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
            assert_same(a, read_context(r, chunk));
            TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
            assert_same(b, read_context(r, chunk));
            TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
            TEST_ASSERT_TRUE(e.kind() == "empty");
            assert_same("", read_context(r, chunk));
            TEST_ASSERT_FALSE(r.try_next(e).value_or(true));
        }
    }
}

TEST_CASE("fault_store gives no context for an empty buffer, then goes on", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("a"), lines(0, 5));
    auto r = faults.read();
    fault e;
    TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
    std::array<char, 16> buf;
    auto none = r.try_next_context(std::span(buf).first(0));
    TEST_ASSERT_TRUE(none.has_value());
    TEST_ASSERT_EQUAL_size_t(0, *none);
    TEST_ASSERT_EQUAL_size_t(16, r.try_next_context(buf).value_or(0));
    TEST_ASSERT_EQUAL_size_t(0, r.try_next_context(std::span(buf).first(0)).value_or(1));
    assert_same(lines(0, 5).substr(16), read_context(r, 16));
}

TEST_CASE("fault_store gives no context without an entry", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("a"), lines(0, 5));
    auto r = faults.read();
    std::array<char, 16> buf;
    // Before the first entry.
    TEST_ASSERT_EQUAL_size_t(0, r.try_next_context(buf).value_or(1));
    fault e;
    TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
    assert_same(lines(0, 5), read_context(r, 16));
    // After all of the context has been given, asking again gives none.
    TEST_ASSERT_EQUAL_size_t(0, r.try_next_context(buf).value_or(1));
    // After the last entry.
    TEST_ASSERT_FALSE(r.try_next(e).value_or(true));
    TEST_ASSERT_EQUAL_size_t(0, r.try_next_context(buf).value_or(1));
    // An empty store gives none either.
    ram_sector_store empty_store(small_sectors, small_sector);
    auto empty = open_store(empty_store).read();
    TEST_ASSERT_EQUAL_size_t(0, empty.try_next_context(buf).value_or(1));
    TEST_ASSERT_FALSE(empty.try_next(e).value_or(true));
    TEST_ASSERT_EQUAL_size_t(0, empty.try_next_context(buf).value_or(1));
}

TEST_CASE("fault_store marks a damaged context before giving any of it", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("first"), lines(0, 30));
    record(faults, make_fault("second"), lines(100, 30));
    // Damage the last byte of the second entry's context. The byte lies past the first piece in
    // which the context is checked.
    store.bytes()[small_slot + fault_header_size + 329] = std::byte{'#'};
    auto r = faults.read();
    fault e;
    TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
    TEST_ASSERT_FALSE(e.damaged());
    // The first entry's context is left unread.
    TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
    TEST_ASSERT_TRUE(e.kind() == "second");
    TEST_ASSERT_TRUE(e.damaged());
    // The context is given as it is stored, including the damaged byte.
    std::string expected = lines(100, 30);
    expected[329] = '#';
    assert_same(expected, read_context(r, 64));
}

TEST_CASE("fault_store ends a context when its entry is overwritten", "[persistent_log]") {
    // The store is full, so the next record overwrites the oldest entry, which is the reader's
    // first.
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    for (int i = 0; i < 4; ++i) {
        record(faults, make_fault("old"), lines(i * 10, 20));
    }
    std::array<char, 16> buf;
    fault e;

    // The entry is overwritten while its context is being read.
    auto r = faults.read();
    TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
    TEST_ASSERT_EQUAL_UINT32(0, e.sequence());
    TEST_ASSERT_EQUAL_size_t(16, r.try_next_context(buf).value_or(0));
    record(faults, make_fault("new"), lines(500, 20));
    TEST_ASSERT_EQUAL_size_t(0, r.try_next_context(buf).value_or(1));
    // The next entry is complete.
    TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
    TEST_ASSERT_EQUAL_UINT32(1, e.sequence());
    assert_same(lines(10, 20), read_context(r));

    // The entry is overwritten before any of its context is read.
    auto fresh = faults.read();
    TEST_ASSERT_TRUE(fresh.try_next(e).value_or(false));
    TEST_ASSERT_EQUAL_UINT32(1, e.sequence());
    record(faults, make_fault("newer"), lines(700, 20));
    TEST_ASSERT_EQUAL_size_t(0, fresh.try_next_context(buf).value_or(1));
    TEST_ASSERT_TRUE(fresh.try_next(e).value_or(false));
    TEST_ASSERT_EQUAL_UINT32(2, e.sequence());
    assert_same(lines(20, 20), read_context(fresh));

    // The entry is overwritten by a record that fails after erasing the slot.
    auto again = faults.read();
    TEST_ASSERT_TRUE(again.try_next(e).value_or(false));
    TEST_ASSERT_EQUAL_UINT32(2, e.sequence());
    store.cut_after(10);
    TEST_ASSERT_FALSE(faults.try_record(make_fault("cut"), lines(600, 3)).has_value());
    store.restore_power();
    TEST_ASSERT_EQUAL_size_t(0, again.try_next_context(buf).value_or(1));
    TEST_ASSERT_TRUE(again.try_next(e).value_or(false));
    TEST_ASSERT_EQUAL_UINT32(3, e.sequence());
    assert_same(lines(30, 20), read_context(again));
}

TEST_CASE("fault_store keeps a context while other slots are recorded", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("a"), lines(0, 20));
    auto r = faults.read();
    fault e;
    TEST_ASSERT_TRUE(r.try_next(e).value_or(false));
    std::array<char, 16> buf;
    TEST_ASSERT_EQUAL_size_t(16, r.try_next_context(buf).value_or(0));
    // Record an entry and a clear in the empty slots. The entry's slot still holds the entry.
    record(faults, make_fault("b"), lines(100, 3));
    clear(faults);
    assert_same(lines(0, 20).substr(16), read_context(r, 16));
}

TEST_CASE(
    "fault_store lists an entry whose context is damaged, with its header",
    "[persistent_log]"
) {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("first"), lines(0, 5));
    record(faults, make_fault("second"), lines(10, 5));
    // Damage a byte of the second entry's context.
    store.bytes()[small_slot + fault_header_size + 20] = std::byte{'#'};

    auto got = list_all(open_store(store));
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_FALSE(got[0].entry.damaged());
    TEST_ASSERT_TRUE(got[1].entry.damaged());
    TEST_ASSERT_TRUE(got[1].line.ends_with(" context=55 damaged=1"));
    TEST_ASSERT_EQUAL_size_t(55, got[1].context.size());
    TEST_ASSERT_EQUAL_size_t(2, count(faults));
}

TEST_CASE("fault_store treats a slot whose header is damaged as empty", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("first"));
    record(faults, make_fault("second"));
    store.bytes()[small_slot + 100] = std::byte{0x55}; // in the detail field
    auto reopened = open_store(store);
    assert_seqs({0}, reopened);
    // The next entry is written after the newest good entry, over the damaged slot.
    TEST_ASSERT_EQUAL_UINT32(1, record(reopened, make_fault("third")));
    assert_seqs({0, 1}, open_store(store));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

// =============================================================================
// The ring
// =============================================================================

TEST_CASE("fault_store overwrites the oldest entry when the ring wraps", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL_UINT32(i, record(faults, make_fault("panic"), lines(i, 3)));
    }
    assert_seqs({6, 7, 8, 9}, faults);
    auto got = list_all(faults);
    for (size_t i = 0; i < got.size(); ++i) {
        assert_same(lines(6 + static_cast<int>(i), 3), got[i].context);
    }
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("fault_store continues the sequence and the ring after reopening", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    {
        auto faults = open_store(store);
        for (int i = 0; i < 6; ++i) {
            record(faults, make_fault("panic"));
        }
    }
    auto faults = open_store(store);
    assert_seqs({2, 3, 4, 5}, faults);
    // Seq 6 goes in slot 2, which follows seq 5's slot 1, and overwrites seq 2.
    TEST_ASSERT_EQUAL_UINT32(6, record(faults, make_fault("next")));
    assert_seqs({3, 4, 5, 6}, open_store(store));
    std::array<std::byte, 12> h;
    TEST_ASSERT_TRUE(store.try_read(2 * small_slot, h).has_value());
    TEST_ASSERT_EQUAL_UINT32(6, std::to_integer<uint32_t>(h[8]));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE(
    "fault_store skips past a slot whose erase was cut after its header",
    "[persistent_log]"
) {
    ram_sector_store store(small_sectors, small_sector);
    {
        auto faults = open_store(store);
        for (int i = 0; i < 5; ++i) {
            record(faults, make_fault("panic"), lines(i, 20));
        }
    }
    // The next entry goes in slot 1, over seq 1. Erase the slot's first four sectors, which hold
    // the header, as if the power was cut while the fifth sector was being erased. The slot is
    // then empty.
    for (size_t i = 0; i < header_sectors; ++i) {
        TEST_ASSERT_TRUE(store.try_erase_sector(small_slot / small_sector + i).has_value());
    }
    auto faults = open_store(store);
    assert_seqs({2, 3, 4}, faults);
    TEST_ASSERT_EQUAL_UINT32(5, record(faults, make_fault("next")));
    assert_seqs({2, 3, 4, 5}, open_store(store));

    // When a context sector is erased but the header is left, the entry is listed as damaged.
    const size_t context_sector = 2 * small_slot / small_sector + header_sectors;
    TEST_ASSERT_TRUE(store.try_erase_sector(context_sector).has_value());
    auto got = list_all(open_store(store));
    TEST_ASSERT_EQUAL_size_t(4, got.size());
    TEST_ASSERT_TRUE(got[0].entry.damaged());
    TEST_ASSERT_FALSE(got[1].entry.damaged());
}

TEST_CASE("fault_store retries the same slot after a failed record", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    for (int i = 0; i < 4; ++i) {
        record(faults, make_fault("panic"), lines(i, 3));
    }
    // The power is cut after the erase completes, while the context is partly written, so the
    // oldest entry is gone.
    store.cut_after(10);
    TEST_ASSERT_FALSE(faults.try_record(make_fault("cut"), lines(50, 3)).has_value());
    store.restore_power();
    TEST_ASSERT_EQUAL_size_t(3, count(faults));
    assert_seqs({1, 2, 3}, faults);
    TEST_ASSERT_EQUAL_UINT32(4, record(faults, make_fault("again"), lines(60, 3)));
    assert_seqs({1, 2, 3, 4}, faults);
    assert_same(lines(60, 3), list_all(open_store(store)).back().context);
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

// =============================================================================
// Clearing
// =============================================================================

TEST_CASE("fault_store clear hides every entry before it", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("before"));
    record(faults, make_fault("before"));
    size_t erases = store.erases();
    clear(faults);
    // A clear records one entry with no context, so it erases only the sectors of its header.
    TEST_ASSERT_EQUAL_size_t(header_sectors, store.erases() - erases);
    assert_seqs({}, faults);
    assert_seqs({}, open_store(store));
    TEST_ASSERT_EQUAL_UINT32(3, record(faults, make_fault("after")));
    assert_seqs({3}, faults);
    assert_seqs({3}, open_store(store));
}

TEST_CASE("fault_store clear holds as the ring wraps over it", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    {
        auto faults = open_store(store);
        for (int i = 0; i < 6; ++i) {
            record(faults, make_fault("before"));
        }
        clear(faults); // seq 6, in slot 2
    }
    // Record entries after the clear, and reopen the store before each one. Nothing from before
    // the clear comes back, either after seq 10 overwrites the cleared entry or as the ring wraps
    // around again.
    std::vector<uint32_t> expected;
    for (uint32_t seq = 7; seq < 16; ++seq) {
        auto faults = open_store(store);
        TEST_ASSERT_EQUAL_UINT32(seq, record(faults, make_fault("after")));
        expected.push_back(seq);
        if (expected.size() > 4) {
            expected.erase(expected.begin());
        }
        auto check = [&](const fault_store& from) {
            for (const auto& l : list_all(from)) {
                TEST_ASSERT_TRUE(l.entry.kind() == "after");
            }
            assert_seqs(expected, from);
        };
        check(faults);
        check(open_store(store));
    }
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("fault_store reads from the newest clear", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("first"));
    clear(faults);
    record(faults, make_fault("second"));
    clear(faults);
    clear(faults);
    TEST_ASSERT_EQUAL_UINT32(5, record(faults, make_fault("third")));
    assert_seqs({5}, faults);
    assert_seqs({5}, open_store(store));
}

TEST_CASE("fault_store clear works on an empty store", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    clear(faults);
    assert_seqs({}, faults);
    TEST_ASSERT_EQUAL_UINT32(1, record(faults, make_fault("one")));
    assert_seqs({1}, open_store(store));
}

// =============================================================================
// The last core dump taken
// =============================================================================

namespace {

fault_input dump_fault(std::string_view kind, uint32_t dump_crc) {
    fault_input f = make_fault(kind);
    f.dump_crc = dump_crc;
    return f;
}

void assert_dump_crc(std::optional<uint32_t> expected, const fault_store& faults) {
    const std::optional<uint32_t> got = faults.last_dump_crc();
    TEST_ASSERT_EQUAL(expected.has_value(), got.has_value());
    if (expected) {
        TEST_ASSERT_EQUAL_HEX32(*expected, *got);
    }
}

} // namespace

TEST_CASE("fault_store has no dump CRC until a fault gives one", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    assert_dump_crc(std::nullopt, faults);
    record(faults, make_fault("plain"));
    clear(faults);
    record(faults, make_fault("plain"));
    assert_dump_crc(std::nullopt, faults);
    assert_dump_crc(std::nullopt, open_store(store));
}

TEST_CASE("fault_store keeps the last dump CRC across a reopen", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        ram_sector_store store = make_store(encrypted, small_sectors, small_sector);
        {
            auto faults = open_store(store);
            record(faults, dump_fault("panic", 0xC0DE0001));
            assert_dump_crc(0xC0DE0001, faults);
        }
        assert_dump_crc(0xC0DE0001, open_store(store));
        // A newer dump replaces it.
        {
            auto faults = open_store(store);
            record(faults, dump_fault("panic", 0xC0DE0002));
            assert_dump_crc(0xC0DE0002, faults);
        }
        assert_dump_crc(0xC0DE0002, open_store(store));
        assert_seqs({0, 1}, open_store(store));
    }
}

TEST_CASE(
    "fault_store carries the last dump CRC through plain faults, a clear and the ring wrapping",
    "[persistent_log]"
) {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, dump_fault("panic", 0xC0DE0011));
    // More plain faults than there are slots overwrite the entry that took the dump.
    for (size_t i = 0; i < faults.slots() + 2; ++i) {
        record(faults, make_fault("plain"));
    }
    for (const auto& l : list_all(faults)) {
        TEST_ASSERT_TRUE(l.entry.kind() == "plain");
    }
    assert_dump_crc(0xC0DE0011, faults);
    assert_dump_crc(0xC0DE0011, open_store(store));
    // A clear carries it too, and a reopen reads it from the clear, which hides every entry.
    clear(faults);
    assert_dump_crc(0xC0DE0011, faults);
    {
        auto reopened = open_store(store);
        assert_seqs({}, reopened);
        assert_dump_crc(0xC0DE0011, reopened);
    }
    // A fault that gives a newer dump replaces it, and plain faults then carry the newer one.
    record(faults, dump_fault("panic", 0xC0DE0012));
    record(faults, make_fault("plain"));
    assert_dump_crc(0xC0DE0012, faults);
    assert_dump_crc(0xC0DE0012, open_store(store));
    TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
}

TEST_CASE("fault_store keeps the last dump CRC when a record fails", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, dump_fault("panic", 0xC0DE0021));
    // The power is cut while the header is written, so the entry that gives the newer dump isn't
    // recorded.
    store.cut_after(fault_header_size / 2);
    TEST_ASSERT_FALSE(faults.try_record(dump_fault("panic", 0xC0DE0022)).has_value());
    store.restore_power();
    assert_dump_crc(0xC0DE0021, faults);
    assert_dump_crc(0xC0DE0021, open_store(store));
    // The next record that succeeds takes its place.
    record(faults, dump_fault("panic", 0xC0DE0022));
    assert_dump_crc(0xC0DE0022, open_store(store));
}

TEST_CASE("fault_store reads a dump CRC flag other than 1 as no CRC", "[persistent_log]") {
    for (uint32_t flag : {0u, 1u, 2u, 0xFFFFFFFFu}) {
        ram_sector_store store(small_sectors, small_sector);
        // A v1 header for seq 0, with the flag at 356 and a CRC at 360, sealed again.
        auto h = slot_header("PFLT", 1, 0);
        put_u32(h, 356, flag);
        put_u32(h, 360, 0xC0DE0031);
        put_u32(h, 508, esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(h.data()), 508));
        put_slot_header(store, 0, h);
        auto faults = open_store(store);
        assert_seqs({0}, faults);
        assert_dump_crc(flag == 1 ? std::optional<uint32_t>(0xC0DE0031) : std::nullopt, faults);
    }
}

// =============================================================================
// Power cuts
// =============================================================================

namespace {

void power_cut_sweep(bool encrypted) {
    // Four slots, each of two 1024-byte sectors. When a power cut stops an erase, the sector keeps
    // its first half. That half holds a complete old header, so the old entry may remain, with its
    // context damaged.
    constexpr size_t sector = 1024;
    constexpr size_t slot = 2 * sector;
    constexpr size_t slots = 4;
    ram_sector_store base = make_store(encrypted, slots * 2, sector);
    {
        auto faults = open_store(base, slot);
        for (int i = 0; i < 5; ++i) {
            record(faults, make_fault("old"), lines(i * 10, 10, "old"));
        }
    }
    constexpr uint32_t base_next = 5;

    // Each step records an entry, or records a clear when the step has no kind. Together, the steps
    // wrap the ring, clear the store, and overwrite the cleared entry (at step 6, the fourth step
    // after the clear).
    struct step {
        std::string_view kind;
        std::string a;
        std::string b;
    };
    std::vector<step> steps = {
        {"panic", lines(0, 14), ""},
        {"task_wdt", lines(100, 64), ""}, // 704 bytes, which reach into the slot's second sector
        {"", "", ""},
        {"brownout", lines(200, 5), lines(205, 7)},
        {"connect_start", lines(300, 3), ""},
        {"sd_write", "", ""},
        {"panic", lines(400, 9), ""},
        {"bus", "", lines(500, 4)},
    };
    auto run = [&](fault_store& faults, const step& s) -> idfxx::result<void> {
        if (s.kind.empty()) {
            return faults.try_clear();
        }
        auto r = faults.try_record(make_fault(s.kind, s.a.substr(0, 20)), s.a, s.b);
        if (!r) {
            return idfxx::error(r.error());
        }
        return {};
    };

    // What a reader lists before each step and after the last step, when the power is never cut.
    std::vector<std::vector<listed>> before;
    {
        ram_sector_store store = base;
        auto faults = open_store(store, slot);
        before.push_back(list_all(faults));
        for (const auto& s : steps) {
            TEST_ASSERT_TRUE(run(faults, s).has_value());
            before.push_back(list_all(faults));
        }
    }

    size_t runs = 0;
    size_t damaged_seen = 0;
    size_t lost_seen = 0;
    bool completed = false;
    for (size_t budget = 0; budget < 20000 && !completed; ++budget) {
        ram_sector_store store = base;
        auto faults = open_store(store, slot);
        store.cut_after(budget);
        size_t at = steps.size();
        for (size_t i = 0; i < steps.size(); ++i) {
            if (!run(faults, steps[i])) {
                at = i;
                break;
            }
        }
        if (at == steps.size()) {
            completed = true;
            break;
        }
        ++runs;
        store.restore_power();

        // The reader lists what it listed before the interrupted step, except for the oldest
        // entry, in the slot that the step was going to write. That entry may be gone, or have a
        // damaged context. No part of the interrupted entry is listed.
        uint32_t in_flight = base_next + static_cast<uint32_t>(at);
        uint32_t oldest = in_flight - slots;
        auto reopened = open_store(store, slot);
        // A header that a power cut interrupted reads as torn, never as a newer layout.
        TEST_ASSERT_FALSE(reopened.found_newer_format());
        auto got = list_all(reopened);
        TEST_ASSERT_EQUAL_size_t(got.size(), count(reopened));
        const auto& was = before[at];
        size_t g = 0;
        for (const auto& w : was) {
            if (g < got.size() && got[g].entry.sequence() == w.entry.sequence()) {
                if (w.entry.sequence() == oldest && got[g].entry.damaged()) {
                    std::string line = w.line;
                    line += " damaged=1";
                    assert_same(line, got[g].line);
                    ++damaged_seen;
                } else {
                    assert_same(w.line, got[g].line);
                    assert_same(w.context, got[g].context);
                }
                ++g;
            } else {
                if (w.entry.sequence() != oldest) {
                    std::printf(
                        "%s budget %u: entry %u missing\n",
                        encrypted ? "encrypted" : "plain",
                        unsigned(budget),
                        unsigned(w.entry.sequence())
                    );
                }
                TEST_ASSERT_EQUAL_UINT32(oldest, w.entry.sequence());
                ++lost_seen;
            }
        }
        if (g != got.size()) {
            std::printf(
                "%s budget %u: unexpected entry %u\n",
                encrypted ? "encrypted" : "plain",
                unsigned(budget),
                unsigned(got[g].entry.sequence())
            );
        }
        TEST_ASSERT_EQUAL_size_t(got.size(), g);

        // Recording again uses the slot and sequence number that the interrupted step was going
        // to use. Nothing is ever written over bytes that weren't erased.
        TEST_ASSERT_EQUAL_UINT32(in_flight, record(reopened, make_fault("after"), "after\n"));
        auto again = list_all(reopened);
        TEST_ASSERT_FALSE(again.empty());
        TEST_ASSERT_EQUAL_UINT32(in_flight, again.back().entry.sequence());
        assert_same("after\n", again.back().context);
        size_t kept = 0;
        for (const auto& l : got) {
            if (l.entry.sequence() != oldest) {
                TEST_ASSERT_TRUE(kept < again.size() - 1);
                assert_same(l.line, again[kept].line);
                ++kept;
            }
        }
        TEST_ASSERT_EQUAL_size_t(again.size() - 1, kept);
        auto reread = list_all(open_store(store, slot));
        TEST_ASSERT_EQUAL_size_t(again.size(), reread.size());
        for (size_t i = 0; i < again.size(); ++i) {
            assert_same(again[i].line, reread[i].line);
        }
        TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    }
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_GREATER_THAN_size_t(2000, runs);
    // The sweep saw the oldest entry both damaged and gone.
    TEST_ASSERT_GREATER_THAN_size_t(0, damaged_seen);
    TEST_ASSERT_GREATER_THAN_size_t(0, lost_seen);
}

} // namespace

TEST_CASE(
    "fault_store stays readable and consistent with a power cut at every byte",
    "[persistent_log]"
) {
    power_cut_sweep(false);
}

TEST_CASE(
    "fault_store stays readable and consistent with a power cut at every byte, encrypted",
    "[persistent_log]"
) {
    power_cut_sweep(true);
}

// =============================================================================
// The header line
// =============================================================================

TEST_CASE("fault formats escaping what would break the line", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    fault_input f = make_fault("panic", "say \"hi\"\\ now\nnext\tline\r\x01\x7f caf\xC3\xA9");
    f.version = "1.2\"x";
    f.task = "t\"\n";
    record(faults, f, "context\n");
    auto got = list_all(faults);
    assert_same(
        "fault seq=0 kind=panic boot=4 wake=17 utc=2026-11-20T10:31:02Z uptime=1234 "
        "version=\"1.2\\\"x\" detail=\"say \\\"hi\\\"\\\\ now\\nnext\\tline\\r\\x01\\x7f "
        "caf\xC3\xA9\" elf=0000000000000000 task=\"t\\\"\\n\" pc=0x00000000 context=8",
        got[0].line
    );
}

namespace {

// Returns a fault whose fields are all as long as they can be, and whose characters all need
// escaping.
fault longest_fault() {
    fault e;
    auto& f = detail::fault_access::fields(e);
    f.seq = 0xFFFFFFFF;
    f.boot = 0xFFFFFFFF;
    f.wake = 0xFFFFFFFF;
    f.uptime = std::chrono::milliseconds(0xFFFFFFFF);
    f.context_size = 0xFFFFFFFF;
    f.damaged = true;
    f.wall = std::chrono::sys_days{std::chrono::December / 31 / 2200} + 23h + 59min + 59s;
    f.kind.fill('\x01');
    f.version.fill('\x01');
    f.detail.fill('\x01');
    f.task.fill('\x01');
    f.kind.back() = f.version.back() = f.detail.back() = f.task.back() = '\0';
    f.elf_sha256.fill(std::byte{0xFF});
    f.exception_pc = 0xFFFFFFFF;
    f.frames.fill(0xFFFFFFFF);
    // The frame count is larger than the number of frames, so only the frames that exist are
    // written.
    f.frame_count = 0xFFFFFFFF;
    return e;
}

} // namespace

TEST_CASE("fault formats the longest line within max_fault_line_size", "[persistent_log]") {
    const fault e = longest_fault();
    TEST_ASSERT_EQUAL_size_t(max_fault_frames, e.frames().size());
    std::string line = header_line(e);
    TEST_ASSERT_EQUAL_size_t(1289, line.size());
    TEST_ASSERT_TRUE(line.find("utc=2200-12-31T23:59:59Z") != std::string::npos);
    TEST_ASSERT_TRUE(line.ends_with(",0xffffffff context=4294967295 damaged=1"));
}

TEST_CASE("fault formats an empty fault", "[persistent_log]") {
    assert_same(
        "fault seq=0 kind= boot=0 wake=0 utc=none uptime=0 version=\"\" detail=\"\" "
        "elf=0000000000000000 task=\"\" pc=0x00000000 context=0",
        header_line(fault())
    );
}

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<fault, char>);

TEST_CASE("fault formats into a buffer of max_fault_line_size", "[persistent_log]") {
    const fault e = longest_fault();
    const std::string line = header_line(e);
    TEST_ASSERT_EQUAL_size_t(line.size(), std::formatted_size("{}", e));
    std::array<char, max_fault_line_size> buf;
    auto r = std::format_to_n(buf.data(), buf.size(), "{}", e);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(line.size()), static_cast<int>(r.size));
    assert_same(line, std::string(buf.data(), r.out));
    // A smaller buffer receives as much of the line as fits.
    auto cut = std::format_to_n(buf.data(), 100, "{}", e);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(line.size()), static_cast<int>(cut.size));
    TEST_ASSERT_TRUE(cut.out == buf.data() + 100);
    assert_same(line.substr(0, 100), std::string(buf.data(), 100));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("fault formatting refuses a format spec", "[persistent_log]") {
    const fault e;
    bool refused = false;
    try {
        (void)std::vformat("{:x}", std::make_format_args(e));
    } catch (const std::format_error&) {
        refused = true;
    }
    TEST_ASSERT_TRUE(refused);
}
#endif
#endif // CONFIG_IDFXX_STD_FORMAT

// =============================================================================
// Reading while entries are recorded
// =============================================================================

TEST_CASE("fault_store reader leaves out entries recorded after it began", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("a"), lines(0, 3));
    record(faults, make_fault("b"), lines(10, 3));
    // The store isn't full, so the next entry goes in an empty slot after the entries that the
    // reader lists.
    auto r = faults.read();
    record(faults, make_fault("late"), lines(20, 3));
    auto first = next_listed(r);
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, first->entry.sequence());
    record(faults, make_fault("later"), lines(30, 3));
    auto second = next_listed(r);
    TEST_ASSERT_TRUE(second.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, second->entry.sequence());
    assert_same(lines(10, 3), second->context);
    TEST_ASSERT_FALSE(next_listed(r).has_value());
}

TEST_CASE("fault_store reader skips slots overwritten while it reads", "[persistent_log]") {
    // At every point while a reader lists a full store, record 1 to 5 entries. Each new entry
    // takes the oldest slot, which the reader has either read already or not yet reached. The
    // reader lists only the entries that the store held when the reader began, oldest first, each
    // with its own context. It doesn't list an entry that was overwritten before the reader
    // reached it.
    for (size_t pause = 0; pause <= 4; ++pause) {
        for (uint32_t more = 1; more <= 5; ++more) {
            ram_sector_store store(small_sectors, small_sector);
            auto faults = open_store(store);
            for (int i = 0; i < 6; ++i) {
                record(faults, make_fault("old"), lines(i * 10, 3 + i));
            }
            // The store holds entries 2 to 5, in slots 2, 3, 0 and 1.
            auto r = faults.read();
            std::vector<listed> got;
            for (size_t i = 0; i < pause; ++i) {
                if (auto l = next_listed(r)) {
                    got.push_back(std::move(*l));
                }
            }
            for (uint32_t i = 0; i < more; ++i) {
                record(faults, make_fault("new"), lines(500 + static_cast<int>(i), 20));
            }
            while (auto l = next_listed(r)) {
                got.push_back(std::move(*l));
            }
            // The reader lists the first `pause` of entries 2 to 5, which it read before the new
            // records, and then each of the rest that wasn't overwritten.
            std::vector<uint32_t> expected;
            for (uint32_t seq = 2; seq <= 5; ++seq) {
                if (seq - 2 < pause || seq - 2 >= more) {
                    expected.push_back(seq);
                }
            }
            TEST_ASSERT_EQUAL_size_t(expected.size(), got.size());
            for (size_t i = 0; i < got.size(); ++i) {
                const auto seq = got[i].entry.sequence();
                TEST_ASSERT_EQUAL_UINT32(expected[i], seq);
                TEST_ASSERT_TRUE(got[i].entry.kind() == "old");
                TEST_ASSERT_FALSE(got[i].entry.damaged());
                assert_same(
                    lines(static_cast<int>(seq) * 10, 3 + static_cast<int>(seq)), got[i].context
                );
            }
        }
    }
}

TEST_CASE("fault_store reader lists on across a clear", "[persistent_log]") {
    ram_sector_store store(small_sectors, small_sector);
    auto faults = open_store(store);
    record(faults, make_fault("a"), lines(0, 3));
    record(faults, make_fault("b"), lines(10, 3));
    record(faults, make_fault("c"), lines(20, 3));
    auto r = faults.read();
    TEST_ASSERT_TRUE(next_listed(r).has_value());
    // The clear goes in the empty fourth slot, and the reader began before the clear.
    clear(faults);
    auto b = next_listed(r);
    auto c = next_listed(r);
    TEST_ASSERT_TRUE(b.has_value() && c.has_value());
    TEST_ASSERT_TRUE(b->entry.kind() == "b");
    TEST_ASSERT_TRUE(c->entry.kind() == "c");
    TEST_ASSERT_FALSE(next_listed(r).has_value());
    TEST_ASSERT_EQUAL_size_t(0, list_all(faults).size());
}

// =============================================================================
// Blocks, encryption and layouts
// =============================================================================

TEST_CASE("fault_store pads the context to a block, and reads it back", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        ram_sector_store store = make_store(encrypted, small_sectors, small_sector);
        auto faults = open_store(store);
        // Contexts of 37 and 330 bytes. The longer one is more than the 256 bytes that go through
        // in one piece, and neither is a whole number of blocks.
        std::string short_text = lines(0, 3) + "abcd";
        std::string long_text = lines(10, 30);
        fault_input f = make_fault("panic");
        f.task = "wifi";
        f.exception_pc = 0x400d1234;
        const std::array<uint32_t, 2> frames = {0x400d1234, 0x400d5678};
        f.frames = frames;
        record(faults, f, short_text);
        record(faults, make_fault("bus"), long_text);

        // The last block of each context is padded with 0xFF, and the slot is erased after it.
        std::array<std::byte, 16> block;
        size_t pad_at = fault_header_size + 32;
        const auto erased = [](std::span<const std::byte> bytes) {
            return std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0xFF}; });
        };
        TEST_ASSERT_TRUE(store.try_read(pad_at, block).has_value());
        TEST_ASSERT_TRUE(std::to_integer<char>(block[4]) == 'd');
        TEST_ASSERT_TRUE(erased(std::span(block).subspan(5)));
        TEST_ASSERT_TRUE(store.try_read_raw(pad_at + 16, block).has_value());
        TEST_ASSERT_TRUE(erased(block));
        const size_t after_long = small_slot + fault_header_size + 336;
        TEST_ASSERT_TRUE(store.try_read_raw(after_long, block).has_value());
        TEST_ASSERT_TRUE(erased(block));

        auto check = [&](const fault_store& from) {
            auto got = list_all(from);
            TEST_ASSERT_EQUAL_size_t(2, got.size());
            assert_same(short_text, got[0].context);
            assert_same(long_text, got[1].context);
            TEST_ASSERT_FALSE(got[0].entry.damaged());
            TEST_ASSERT_FALSE(got[1].entry.damaged());
            TEST_ASSERT_TRUE(got[0].entry.task() == "wifi");
            TEST_ASSERT_EQUAL_HEX32(0x400d1234, got[0].entry.exception_pc());
            TEST_ASSERT_TRUE(std::ranges::equal(frames, got[0].entry.frames()));
            TEST_ASSERT_TRUE(got[0].line.ends_with(
                " task=\"wifi\" pc=0x400d1234 frames=0x400d1234,0x400d5678 context=37"
            ));
            TEST_ASSERT_EQUAL_UINT32(0, got[1].entry.frames().size());
            TEST_ASSERT_TRUE(got[1].line.ends_with(" task=\"\" pc=0x00000000 context=330"));
        };
        check(faults);
        check(open_store(store));
        TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    }
}

TEST_CASE("fault_store treats a slot a power cut tore as empty, encrypted", "[persistent_log]") {
    // Cut the power in the context's last block, and in the header's first and last blocks.
    for (size_t cut : {size_t{37}, size_t{48 + 3}, size_t{48 + 511}}) {
        ram_sector_store store(ram_sector_store::encrypted, small_sectors, small_sector);
        auto faults = open_store(store);
        record(faults, make_fault("first"), lines(0, 3));
        store.cut_after(cut);
        TEST_ASSERT_FALSE(faults.try_record(make_fault("cut"), lines(10, 4)).has_value());
        store.restore_power();
        auto reopened = open_store(store);
        assert_seqs({0}, reopened);
        TEST_ASSERT_FALSE(reopened.found_newer_format());
        TEST_ASSERT_EQUAL_UINT32(1, record(reopened, make_fault("again"), lines(20, 4)));
        assert_same(lines(20, 4), list_all(open_store(store)).back().context);
        TEST_ASSERT_EQUAL_size_t(0, store.overwrites());
    }
}

TEST_CASE("fault_store reads an entry of a newer layout as an empty slot", "[persistent_log]") {
    for (bool encrypted : {false, true}) {
        ram_sector_store base = make_store(encrypted, small_sectors, small_sector);
        {
            auto faults = open_store(base);
            record(faults, make_fault("first"), lines(0, 3));
            record(faults, make_fault("second"), lines(10, 3));
        }
        TEST_ASSERT_FALSE(open_store(base).found_newer_format());

        // An entry of a later layout, numbered as if it were the newest, is left out, and the
        // store notes that it found a newer format.
        ram_sector_store newer = base;
        put_slot_header(newer, 2, slot_header("PFLT", 2, 100));
        auto faults = open_store(newer);
        TEST_ASSERT_TRUE(faults.found_newer_format());
        assert_seqs({0, 1}, faults);
        // The next entry overwrites it.
        TEST_ASSERT_EQUAL_UINT32(2, record(faults, make_fault("third")));
        assert_seqs({0, 1, 2}, open_store(newer));
        TEST_ASSERT_FALSE(open_store(newer).found_newer_format());

        // A header with another magic ("MFT\x01" here), or with this layout's magic at an older
        // version (0 here), reads as an empty slot and isn't noted.
        for (auto h : {slot_header("MFT\x01", 0, 100), slot_header("PFLT", 0, 100)}) {
            ram_sector_store other = base;
            put_slot_header(other, 2, h);
            auto opened = open_store(other);
            TEST_ASSERT_FALSE(opened.found_newer_format());
            assert_seqs({0, 1}, opened);
        }
        // A header of a newer version that fails its CRC reads as torn, not as newer.
        auto bad = slot_header("PFLT", 2, 100);
        bad[300] ^= std::byte{0x01};
        ram_sector_store torn = base;
        put_slot_header(torn, 2, bad);
        TEST_ASSERT_FALSE(open_store(torn).found_newer_format());
    }
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("fault_store's throwing calls throw what the try_ calls return", "[persistent_log]") {
    auto code_of = [](auto&& call) {
        try {
            call();
        } catch (const std::system_error& e) {
            return e.code();
        }
        return std::error_code();
    };

    ram_sector_store store = make_store(false, small_sectors, small_sector);
    TEST_ASSERT_TRUE(
        code_of([&] { (void)fault_store::open(store, small_slot + 1); }) == idfxx::errc::invalid_arg
    );

    auto faults = fault_store::open(store, small_slot);
    TEST_ASSERT_EQUAL_UINT32(0, faults.record(make_fault("first"), "one\n"));
    TEST_ASSERT_EQUAL_UINT32(1, faults.record(make_fault("second"), "two\n"));
    TEST_ASSERT_EQUAL_size_t(2, faults.count());
    TEST_ASSERT_TRUE(
        code_of([&] { (void)faults.record(make_fault("")); }) == idfxx::errc::invalid_arg
    );

    auto r = faults.read();
    fault e;
    std::array<char, 3> buf;
    TEST_ASSERT_TRUE(r.next(e));
    TEST_ASSERT_TRUE(e.kind() == "first");
    TEST_ASSERT_EQUAL_size_t(3, r.next_context(buf));
    TEST_ASSERT_EQUAL_size_t(1, r.next_context(buf));
    TEST_ASSERT_EQUAL_size_t(0, r.next_context(buf));
    TEST_ASSERT_TRUE(r.next(e));
    TEST_ASSERT_TRUE(e.kind() == "second");
    TEST_ASSERT_FALSE(r.next(e));
    TEST_ASSERT_EQUAL_size_t(0, r.next_context(buf));
    auto again = faults.read();
    TEST_ASSERT_TRUE(again.next(e));

    faults.clear();
    TEST_ASSERT_EQUAL_size_t(0, faults.count());

    store.cut_after(0);
    TEST_ASSERT_TRUE(code_of([&] { faults.clear(); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { (void)faults.count(); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { (void)again.next_context(buf); }) == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(code_of([&] { (void)again.next(e); }) == idfxx::errc::invalid_state);
}
#endif
