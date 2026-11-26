// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_persistent_log's entries: splitting text into entries with next_entry,
// reading each entry's stamp, and the buffer that the entry readers split text in.

#include "sdkconfig.h"

#include "../src/entry_buffer.hpp"
#include "../src/log_line.hpp"
#include "entry_helpers.hpp"
#include "idfxx/persistent_log"
#include "unity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace pl = idfxx::persistent_log;
using namespace std::chrono;
using namespace std::chrono_literals;

static_assert(std::is_trivially_copyable_v<pl::entry>);
static_assert(std::is_default_constructible_v<pl::entry>);
// The wall-clock times are kept to the second, and returned at that precision.
static_assert(
    std::same_as<decltype(pl::entry().wall_time()), std::optional<std::chrono::sys_seconds>>
);
static_assert(
    std::same_as<decltype(pl::fault().wall_time()), std::optional<std::chrono::sys_seconds>>
);
static_assert(std::ranges::borrowed_range<pl::entries>);
static_assert(std::same_as<std::ranges::range_value_t<pl::entries>, pl::entry>);

namespace {

std::string_view stored(const pl::entry& e) {
    return pl::detail::entry_access::stored(e);
}

void assert_same(std::string_view expected, std::string_view actual) {
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    TEST_ASSERT_TRUE(expected == actual);
}

std::vector<pl::entry> entries_of(std::string_view text) {
    std::vector<pl::entry> out;
    pl::entry e;
    while (pl::next_entry(text, e)) {
        out.push_back(e);
    }
    TEST_ASSERT_TRUE(text.empty());
    return out;
}

// The one entry in `text`.
pl::entry only_entry(std::string_view text) {
    auto got = entries_of(text);
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    return got[0];
}

void assert_unstamped(const pl::entry& e, std::string_view text) {
    TEST_ASSERT_FALSE(e.stamped());
    TEST_ASSERT_EQUAL_UINT32(0, e.boot());
    TEST_ASSERT_EQUAL_UINT32(0, e.wake());
    TEST_ASSERT_TRUE(e.uptime() == 0ms);
    TEST_ASSERT_FALSE(e.wall_time().has_value());
    assert_same(text, e.text());
    assert_same(text, stored(e));
}

std::string prefix(
    uint32_t boot,
    uint32_t wake,
    milliseconds uptime,
    std::optional<system_clock::time_point> wall
) {
    std::array<char, pl::max_prefix_size> buf;
    const size_t n = pl::format_prefix(buf, boot, wake, uptime, wall);
    TEST_ASSERT_GREATER_THAN_size_t(0, n);
    return std::string(buf.data(), n);
}

} // namespace

// =============================================================================
// Stamps
// =============================================================================

TEST_CASE("next_entry reads a stamp with a wall time", "[persistent_log]") {
    const auto e = only_entry("12.345 +1234 2026-11-20T10:31:02Z W app: battery low\n");
    TEST_ASSERT_TRUE(e.stamped());
    TEST_ASSERT_EQUAL_UINT32(12, e.boot());
    TEST_ASSERT_EQUAL_UINT32(345, e.wake());
    TEST_ASSERT_TRUE(e.uptime() == 1234ms);
    TEST_ASSERT_TRUE(e.wall_time() == sys_days{2026y / November / 20} + 10h + 31min + 2s);
    assert_same("W app: battery low", e.text());
    assert_same("12.345 +1234 2026-11-20T10:31:02Z W app: battery low", stored(e));
}

TEST_CASE("next_entry reads a stamp without a wall time", "[persistent_log]") {
    const auto e = only_entry("12.0 +80 - wake reason=button\n");
    TEST_ASSERT_TRUE(e.stamped());
    TEST_ASSERT_EQUAL_UINT32(12, e.boot());
    TEST_ASSERT_EQUAL_UINT32(0, e.wake());
    TEST_ASSERT_TRUE(e.uptime() == 80ms);
    TEST_ASSERT_FALSE(e.wall_time().has_value());
    assert_same("wake reason=button", e.text());
}

TEST_CASE("next_entry reads a stamp with no text after it", "[persistent_log]") {
    const auto e = only_entry("1.2 +3 - \n");
    TEST_ASSERT_TRUE(e.stamped());
    assert_same("", e.text());
    assert_same("1.2 +3 - ", stored(e));
}

TEST_CASE("next_entry reads back the stamps that format_prefix writes", "[persistent_log]") {
    const std::array<std::optional<system_clock::time_point>, 4> walls = {
        std::nullopt,
        system_clock::time_point{},
        sys_days{2026y / February / 28} + 23h + 59min + 59s + 999ms,
        sys_days{2262y / January / 1} + 23h + 59min + 59s,
    };
    const std::array<uint32_t, 3> counts = {0, 7, std::numeric_limits<uint32_t>::max()};
    const std::array<milliseconds, 3> times = {0ms, 49h, milliseconds::max()};
    for (const auto& wall : walls) {
        for (uint32_t n : counts) {
            for (milliseconds t : times) {
                const std::string text = prefix(n, n / 2, t, wall) + "text";
                const auto e = only_entry(text);
                TEST_ASSERT_TRUE(e.stamped());
                TEST_ASSERT_EQUAL_UINT32(n, e.boot());
                TEST_ASSERT_EQUAL_UINT32(n / 2, e.wake());
                TEST_ASSERT_TRUE(e.uptime() == t);
                if (wall) {
                    TEST_ASSERT_TRUE(e.wall_time() == floor<seconds>(*wall));
                } else {
                    TEST_ASSERT_FALSE(e.wall_time().has_value());
                }
                assert_same("text", e.text());
            }
        }
    }
}

TEST_CASE("next_entry reads the largest values of a stamp's fields", "[persistent_log]") {
    const auto e = only_entry("4294967295.4294967295 +9223372036854775807 - x\n");
    TEST_ASSERT_TRUE(e.stamped());
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(), e.boot());
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(), e.wake());
    TEST_ASSERT_TRUE(e.uptime() == milliseconds::max());
    assert_same("x", e.text());

    // One more doesn't fit, so the entry isn't stamped.
    for (std::string_view text : {
             "4294967296.0 +1 - x",
             "0.4294967296 +1 - x",
             "0.0 +9223372036854775808 - x",
         }) {
        assert_unstamped(only_entry(text), text);
    }
}

TEST_CASE("next_entry doesn't stamp an entry whose stamp was cut short", "[persistent_log]") {
    const std::string_view stamps[] = {
        "12.345 +1234 2026-11-20T10:31:02Z ",
        "12.345 +1234 - ",
    };
    for (std::string_view stamp : stamps) {
        for (size_t n = 1; n < stamp.size(); ++n) {
            const std::string_view cut = stamp.substr(0, n);
            // With a newline after the cut, and without one.
            assert_unstamped(only_entry(std::string(cut) + "\n"), cut);
            assert_unstamped(only_entry(cut), cut);
        }
    }
    // A cut stamp, then a whole entry on the next line.
    auto got = entries_of("12.345 +12\n3.4 +5 - next\n");
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    assert_unstamped(got[0], "12.345 +12");
    TEST_ASSERT_TRUE(got[1].stamped());
    assert_same("next", got[1].text());
}

TEST_CASE("next_entry doesn't take a line led by digits for a stamp", "[persistent_log]") {
    for (std::string_view text : {
             "2026 was a year",
             "12",
             "12 +5 - a",
             "12. +5 - a",
             "12.3 5 - a",
             "12.3+5 - a",
             "12.3 + - a",
             "12.3 +x - a",
             "12.3 +-5 - a",
             "12.3 +5 -a",
             "12.3 +5  a",
             "12.3 +5 x a",
             "12.-1 +5 - a",
             "12.3 +5 2026-13-01T00:00:00Z a",
             "12.3 +5 2026-02-30T00:00:00Z a",
             "12.3 +5 2026-01-01T24:00:00Z a",
             "12.3 +5 2026-01-01T00:60:00Z a",
             "12.3 +5 2026-01-01T00:00:60Z a",
             "12.3 +5 2026-01-01 00:00:00Z a",
             "12.3 +5 2026-01-01T00:00:00 a",
             "12.3 +5 26-01-01T00:00:00Z a",
             "12.3 +5 2026-01-01T00:00:00Za",
             // Outside what the system clock holds.
             "12.3 +5 9999-12-31T23:59:59Z a",
         }) {
        assert_unstamped(only_entry(text), text);
    }
}

TEST_CASE("next_entry doesn't stamp an entry whose stamp is too long", "[persistent_log]") {
    // Leading zeros make the stamp longer than format_prefix writes. The stamp is valid at the
    // longest size, and not valid one byte past it, or 256 bytes past it.
    const std::string_view tail = "1.0 +5 - x";
    const std::string longest =
        std::string(pl::max_prefix_size - tail.size() + 1, '0') + std::string(tail);
    const auto e = only_entry(longest);
    TEST_ASSERT_TRUE(e.stamped());
    TEST_ASSERT_EQUAL_UINT32(1, e.boot());
    assert_same("x", e.text());
    for (size_t more : {size_t{1}, size_t{256}}) {
        const std::string text = std::string(more, '0') + longest;
        assert_unstamped(only_entry(text), text);
    }
}

// =============================================================================
// Splitting text into entries
// =============================================================================

TEST_CASE("next_entry keeps an entry's later lines with it", "[persistent_log]") {
    auto got =
        entries_of("1.0 +5 - E app: config rejected\n\tssid=home\n\tretry=3\n2.0 +6 - next\n");
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got[0].stamped());
    assert_same("E app: config rejected\n\tssid=home\n\tretry=3", got[0].text());
    assert_same("1.0 +5 - E app: config rejected\n\tssid=home\n\tretry=3", stored(got[0]));
    assert_same("next", got[1].text());
}

TEST_CASE("next_entry returns an entry cut short in its message as it is", "[persistent_log]") {
    auto got = entries_of("1.0 +5 - whole\n2.0 +6 - cut in the mid");
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    TEST_ASSERT_TRUE(got[1].stamped());
    TEST_ASSERT_EQUAL_UINT32(2, got[1].boot());
    assert_same("cut in the mid", got[1].text());
    // Cut after a newline that a tab would have followed.
    got = entries_of("1.0 +5 - first\n\tsecond\n");
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    assert_same("first\n\tsecond", got[0].text());
}

TEST_CASE("next_entry returns lines at the start that start with a tab", "[persistent_log]") {
    auto got = entries_of("\tthe rest\n\tof an entry\n1.0 +5 - next\n");
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    assert_unstamped(got[0], "\tthe rest\n\tof an entry");
    TEST_ASSERT_TRUE(got[1].stamped());
    assert_same("next", got[1].text());
}

TEST_CASE("next_entry skips empty lines, which end the entry before them", "[persistent_log]") {
    auto got = entries_of("\n\n1.0 +5 - a\n\n\n2.0 +6 - b\n\n\tc\n\n");
    TEST_ASSERT_EQUAL_size_t(3, got.size());
    assert_same("a", got[0].text());
    assert_same("b", got[1].text());
    assert_unstamped(got[2], "\tc");
}

TEST_CASE("next_entry finds no entry in empty text", "[persistent_log]") {
    const pl::entry kept = only_entry("1.0 +5 - kept");
    for (std::string_view none : {"", "\n", "\n\n\n"}) {
        std::string_view text = none;
        pl::entry e = kept;
        TEST_ASSERT_FALSE(pl::next_entry(text, e));
        TEST_ASSERT_TRUE(text.empty());
        // The entry is left as it was.
        TEST_ASSERT_TRUE(stored(e).data() == stored(kept).data());
        TEST_ASSERT_EQUAL_size_t(stored(kept).size(), stored(e).size());
    }
}

TEST_CASE("next_entry keeps NUL bytes and carriage returns in the text", "[persistent_log]") {
    using namespace std::string_view_literals;
    auto got = entries_of("1.0 +5 - a\0b\r\n\tc\r\n"sv);
    TEST_ASSERT_EQUAL_size_t(1, got.size());
    assert_same("a\0b\r\n\tc\r"sv, got[0].text());
}

TEST_CASE("an entry made by default is empty and not stamped", "[persistent_log]") {
    const pl::entry e;
    assert_unstamped(e, "");
}

// =============================================================================
// The entries view
// =============================================================================

namespace {

// The stored text of each entry that the entries view returns for `text`.
std::vector<std::string> viewed(std::string_view text) {
    std::vector<std::string> out;
    for (pl::entry e : pl::entries(text)) {
        out.emplace_back(stored(e));
    }
    return out;
}

} // namespace

TEST_CASE("entries splits text as next_entry does", "[persistent_log]") {
    using namespace std::string_view_literals;
    for (std::string_view text : {
             "1.0 +5 - E app: config rejected\n\tssid=home\n\tretry=3\n2.0 +6 - next\n"sv,
             "1.0 +5 - whole\n2.0 +6 - cut in the mid"sv,
             "\tthe rest\n\tof an entry\n1.0 +5 - next\n"sv,
             "\n\n1.0 +5 - a\n\n\n2.0 +6 - b\n\n\tc\n\n"sv,
             "1.0 +5 - a\0b\r\n\tc\r\n"sv,
             "12.345 +12\n3.4 +5 - next\n"sv,
             "no stamp\n"sv,
         }) {
        const auto want = pl::testing::split_entries(text);
        TEST_ASSERT_GREATER_THAN_size_t(0, want.size());
        TEST_ASSERT_TRUE(viewed(text) == want);
    }
    // The entries refer to the text itself.
    const std::string_view text = "1.0 +5 - a\n2.0 +6 - b\n";
    auto it = pl::entries(text).begin();
    TEST_ASSERT_TRUE(stored(*it).data() == text.data());
    TEST_ASSERT_TRUE((*it).stamped());
    TEST_ASSERT_EQUAL_UINT32(1, (*it).boot());
    ++it;
    TEST_ASSERT_TRUE(stored(*it).data() == text.data() + 11);
    TEST_ASSERT_EQUAL_UINT32(2, (*it).boot());
    ++it;
    TEST_ASSERT_TRUE(it == std::default_sentinel);
}

TEST_CASE("entries of empty text is empty", "[persistent_log]") {
    for (std::string_view none : {"", "\n", "\n\n\n"}) {
        const pl::entries view(none);
        TEST_ASSERT_TRUE(view.empty());
        TEST_ASSERT_TRUE(view.begin() == view.end());
        TEST_ASSERT_TRUE(view.begin() == pl::entries::iterator());
        TEST_ASSERT_EQUAL_size_t(0, std::ranges::distance(view));
    }
    const pl::entries made;
    TEST_ASSERT_TRUE(made.empty());
    TEST_ASSERT_TRUE(made.begin() == pl::entries::iterator());
}

TEST_CASE("entries can be iterated more than once", "[persistent_log]") {
    // Bytes of every value, in an order that a simple generator gives, with many newlines and
    // tabs, so that the text has empty lines, entries of several lines and lines led by digits.
    std::string text;
    uint32_t x = 54321;
    for (int i = 0; i < 1500; ++i) {
        x = x * 1103515245 + 12345;
        const uint32_t pick = (x >> 16) % 16;
        text += pick < 3 ? '\n' : pick < 5 ? '\t' : pick < 6 ? '\0' : static_cast<char>(x >> 8);
    }
    const auto want = pl::testing::split_entries(text);
    TEST_ASSERT_GREATER_THAN_size_t(10, want.size());
    const pl::entries view(text);
    // Two passes over one view give the same entries.
    TEST_ASSERT_TRUE(viewed(text) == want);
    std::vector<std::string> again;
    for (pl::entry e : view) {
        again.emplace_back(stored(e));
    }
    TEST_ASSERT_TRUE(again == want);
    TEST_ASSERT_EQUAL_size_t(want.size(), std::ranges::distance(view));
    // A copy of an iterator goes through the same entries as the iterator that it was copied
    // from, and the two are equal at the same entry.
    auto first = view.begin();
    std::ranges::advance(first, 3);
    auto copy = first;
    TEST_ASSERT_TRUE(copy == first);
    ++first;
    TEST_ASSERT_FALSE(copy == first);
    TEST_ASSERT_TRUE(stored(*copy) == want[3]);
    TEST_ASSERT_TRUE(stored(*first) == want[4]);
    TEST_ASSERT_TRUE(stored(*copy++) == want[3]);
    TEST_ASSERT_TRUE(copy == first);
    // An iterator stays valid after its view is destroyed.
    auto kept = pl::entries(text).begin();
    TEST_ASSERT_TRUE(stored(*kept) == want[0]);
    // The end of the text is the same position for every iterator that reaches it.
    auto end = view.begin();
    std::ranges::advance(end, view.end());
    auto other_end = view.begin();
    std::ranges::advance(other_end, static_cast<std::ptrdiff_t>(want.size()));
    TEST_ASSERT_TRUE(end == other_end);
    TEST_ASSERT_TRUE(end == pl::entries::iterator());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
TEST_CASE("std::format writes an entry as it is stored", "[persistent_log]") {
    auto got = entries_of("1.0 +5 2026-11-20T10:31:02Z E app: a\n\tb\n\tc\n12.3 +4\n");
    TEST_ASSERT_EQUAL_size_t(2, got.size());
    assert_same("1.0 +5 2026-11-20T10:31:02Z E app: a\n\tb\n\tc", std::format("{}", got[0]));
    assert_same("12.3 +4", std::format("{}", got[1]));
    assert_same("", std::format("{}", pl::entry{}));
}
#endif

// =============================================================================
// The entry readers' buffer
// =============================================================================

namespace {

// A source that returns `text` in pieces the size of each buffer passed.
struct source {
    std::string_view text;

    idfxx::result<size_t> operator()(std::span<char> out) {
        const size_t n = std::min(out.size(), text.size());
        std::copy_n(text.data(), n, out.data());
        text.remove_prefix(n);
        return n;
    }
};

// The stored text of each entry that a buffer of `size` bytes splits `text` into.
std::vector<std::string> buffered(std::string_view text, size_t size) {
    pl::entry_buffer b(size);
    source s{text};
    std::vector<std::string> out;
    pl::entry e;
    while (true) {
        auto more = b.next(e, s);
        TEST_ASSERT_TRUE(more.has_value());
        if (!*more) {
            return out;
        }
        out.emplace_back(stored(e));
    }
}

using pl::testing::split_entries;

} // namespace

TEST_CASE("entry_buffer splits text as next_entry does, at any buffer size", "[persistent_log]") {
    const std::string_view text = "\n\tleft over\n\tfrom before\n"
                                  "1.0 +5 - E app: config rejected\n\tssid=home\n\tretry=3\n"
                                  "1.0 +6 - one\n"
                                  "\n"
                                  "\tafter an empty line\n"
                                  "1.1 +7 2026-11-20T10:31:02Z two\n\tlines\n"
                                  "1.1 +8 - x\n"
                                  "1.1 +9 - cut";
    const auto want = split_entries(text);
    TEST_ASSERT_EQUAL_size_t(7, want.size());
    size_t longest = 0;
    for (const auto& s : want) {
        longest = std::max(longest, s.size());
    }
    // A buffer that holds the longest entry and the byte after it splits the text the same way,
    // wherever its refills begin.
    for (size_t size = longest + 2; size <= text.size() + 1; ++size) {
        const auto got = buffered(text, size);
        TEST_ASSERT_EQUAL_size_t(want.size(), got.size());
        for (size_t i = 0; i < want.size(); ++i) {
            assert_same(want[i], got[i]);
        }
    }
}

TEST_CASE("entry_buffer returns a line longer than itself in pieces", "[persistent_log]") {
    const std::string line = "1.0 +5 - " + std::string(40, 'x');
    const std::string text = line + "\n2.0 +6 - b\n";
    pl::entry_buffer b(16);
    source s{text};
    pl::entry e;
    std::string joined;
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(b.next(e, s).value_or(false));
        TEST_ASSERT_FALSE(e.stamped());
        joined += stored(e);
    }
    assert_same(line, joined);
    TEST_ASSERT_TRUE(b.next(e, s).value_or(false));
    TEST_ASSERT_TRUE(e.stamped());
    assert_same("b", e.text());
    TEST_ASSERT_FALSE(b.next(e, s).value_or(true));
}

TEST_CASE("entry_buffer returns an entry longer than itself at its lines", "[persistent_log]") {
    const std::string text = "1.0 +5 - a\n\tbbbbbbbbbb\n\tccccccccccc\n2.0 +1 - d\n";
    const auto got = buffered(text, 24);
    TEST_ASSERT_EQUAL_size_t(3, got.size());
    assert_same("1.0 +5 - a\n\tbbbbbbbbbb", got[0]);
    assert_same("\tccccccccccc", got[1]);
    assert_same("2.0 +1 - d", got[2]);
    TEST_ASSERT_TRUE(only_entry(got[0]).stamped());
    TEST_ASSERT_FALSE(only_entry(got[1]).stamped());
}

TEST_CASE("entry_buffer holds the longest entry whole", "[persistent_log]") {
    // An entry of the largest size, with its newline: the largest stamp that a valid wall time
    // allows, then text with as many tabs as it can hold. The stamp is shorter than
    // max_prefix_size, so the text is longer than the most that the log keeps.
    std::string longest = prefix(
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max(),
        milliseconds::max(),
        sys_days{2262y / January / 1}
    );
    longest += "a";
    while (longest.size() + 3 <= pl::max_entry_size - 1) {
        longest += "\n\ta";
    }
    while (longest.size() < pl::max_entry_size - 1) {
        longest += "a";
    }
    TEST_ASSERT_EQUAL_size_t(pl::max_entry_size, longest.size() + 1);
    TEST_ASSERT_TRUE(only_entry(longest).stamped());
    const std::string text = longest + "\n" + longest + "\n1.0 +1 - next\n";
    // The smallest buffer that holds the longest entry and the byte after it, and the size that
    // the readers use.
    for (size_t size : {pl::max_entry_size + 1, pl::entry_buffer_size}) {
        const auto got = buffered(text, size);
        TEST_ASSERT_EQUAL_size_t(3, got.size());
        assert_same(longest, got[0]);
        assert_same(longest, got[1]);
        assert_same("1.0 +1 - next", got[2]);
    }
}

TEST_CASE("entry_buffer passes on the source's error, and forgets on reset", "[persistent_log]") {
    pl::entry_buffer b(32);
    pl::entry e;
    auto failed = b.next(e, [](std::span<char>) -> idfxx::result<size_t> {
        return idfxx::error(idfxx::errc::fail);
    });
    TEST_ASSERT_FALSE(failed.has_value());
    TEST_ASSERT_TRUE(failed.error() == idfxx::errc::fail);

    source s{"1.0 +5 - a\n1.0 +6 - b\n"};
    TEST_ASSERT_TRUE(b.next(e, s).value_or(false));
    assert_same("a", e.text());
    // After a reset, the text held is gone, and the next source is read from its start.
    b.reset();
    source other{"2.0 +1 - c\n"};
    TEST_ASSERT_TRUE(b.next(e, other).value_or(false));
    assert_same("c", e.text());
    TEST_ASSERT_FALSE(b.next(e, other).value_or(true));
}

// =============================================================================
// Counting entries in pieces
// =============================================================================

namespace {

using namespace std::string_view_literals;

// Texts that the splitter tests use: empty lines, a first line that starts with a tab, NUL bytes
// and carriage returns, and no final newline.
const std::array<std::string_view, 6> counted_texts = {
    "\n\tleft over\n\tfrom before\n1.0 +5 - a\n\tb\n\n\n2.0 +6 - c\n\n\td\n\n"sv,
    "1.0 +5 - a\0b\r\n\tc\r\n\0\n\t\0\n"sv,
    "1.0 +5 - whole\n2.0 +6 - cut in the mid"sv,
    "\n\n\n"sv,
    ""sv,
    "x\ny\n\tz\n\n\n\tw"sv,
};

// The offset in `text` where each entry that next_entry() takes from it begins.
std::vector<size_t> entry_offsets(std::string_view text) {
    std::vector<size_t> out;
    std::string_view rest = text;
    pl::entry e;
    while (pl::next_entry(rest, e)) {
        out.push_back(static_cast<size_t>(stored(e).data() - text.data()));
    }
    return out;
}

} // namespace

TEST_CASE(
    "entry_counter counts the entries that next_entry takes, in any pieces",
    "[persistent_log]"
) {
    for (std::string_view text : counted_texts) {
        const auto offsets = entry_offsets(text);
        for (size_t piece = 1; piece <= text.size() + 1; ++piece) {
            // Counting all of the entries.
            pl::detail::entry_counter all;
            for (size_t at = 0; at < text.size(); at += piece) {
                TEST_ASSERT_TRUE(all.add(text.substr(at, piece)) == std::string_view::npos);
            }
            TEST_ASSERT_EQUAL_size_t(offsets.size(), all.count());
            // Finding each entry, wherever the pieces begin and end.
            for (size_t wanted = 0; wanted < offsets.size(); ++wanted) {
                pl::detail::entry_counter c;
                size_t found = std::string_view::npos;
                for (size_t at = 0; at < text.size() && found == std::string_view::npos;
                     at += piece) {
                    const size_t i = c.add(text.substr(at, piece), wanted);
                    if (i != std::string_view::npos) {
                        found = at + i;
                    }
                }
                TEST_ASSERT_EQUAL_size_t(offsets[wanted], found);
                TEST_ASSERT_EQUAL_size_t(wanted, c.count());
            }
        }
    }
}

TEST_CASE("entry_buffer splits arbitrary bytes as next_entry does", "[persistent_log]") {
    // Bytes of every value, in an order that a simple generator gives, with many newlines and
    // tabs so that the text has empty lines, entries of several lines and lines led by digits.
    std::string text;
    uint32_t x = 12345;
    for (int i = 0; i < 1500; ++i) {
        x = x * 1103515245 + 12345;
        const uint32_t pick = (x >> 16) % 16;
        text += pick < 3 ? '\n' : pick < 5 ? '\t' : pick < 6 ? '\0' : static_cast<char>(x >> 8);
    }
    const auto want = split_entries(text);
    size_t longest = 0;
    for (const auto& s : want) {
        longest = std::max(longest, s.size());
    }
    for (size_t size : {longest + 2, longest + 3, longest + 17, text.size() + 1}) {
        const auto got = buffered(text, size);
        TEST_ASSERT_TRUE(got == want);
    }
}
