// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_persistent_log: the line ring, including appends that are stopped between
// their stores, as a reset would stop them.

#include "../src/line_ring.hpp"
#include "sdkconfig.h"
#include "unity.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

using namespace idfxx::persistent_log;

static_assert(std::is_trivially_default_constructible_v<line_ring<16>>);
static_assert(std::is_trivially_copyable_v<line_ring<16>>);
static_assert(std::is_standard_layout_v<line_ring<16>>);
static_assert(line_ring<16>::modulus == uint64_t{1} << 32);
static_assert(line_ring<6144>::modulus % 6144 == 0);
static_assert(line_ring<6144>::modulus <= uint64_t{1} << 32);

namespace {

std::string str(const ring_bytes& b) {
    std::string s(b.first.begin(), b.first.end());
    s.append(b.second.begin(), b.second.end());
    return s;
}

template<size_t N>
line_ring<N> fresh() {
    line_ring<N> r;
    std::memset(&r, 0xA5, sizeof r);
    r.reset();
    return r;
}

// Returns empty pending bytes that end at the index that the caller gives.
pending_bytes ending_at(uint32_t end) {
    pending_bytes p;
    p.end = end;
    return p;
}

void assert_same(std::string_view expected, const std::string& actual) {
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    TEST_ASSERT_TRUE(expected == actual);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
struct stop {};
#endif

} // namespace

TEST_CASE("line_ring is valid after a reset and empty", "[persistent_log]") {
    auto r = fresh<16>();
    TEST_ASSERT_TRUE(r.valid());
    TEST_ASSERT_EQUAL_size_t(0, r.unflushed());
    TEST_ASSERT_EQUAL_size_t(0, r.pending().size());
    TEST_ASSERT_EQUAL_size_t(0, r.tail(100).size());
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
}

TEST_CASE("line_ring rejects random contents", "[persistent_log]") {
    line_ring<6144> r;
    uint32_t x = 12345;
    for (int round = 0; round < 50; ++round) {
        auto* bytes = reinterpret_cast<unsigned char*>(&r);
        for (size_t i = 0; i < sizeof r; ++i) {
            x = x * 1664525 + 1013904223;
            bytes[i] = static_cast<unsigned char>(x >> 24);
        }
        TEST_ASSERT_FALSE(r.valid());
    }
}

TEST_CASE("line_ring rejects indices out of order", "[persistent_log]") {
    auto r = fresh<16>();
    r.append("0123456789");
    TEST_ASSERT_TRUE(r.valid());

    auto bad = r;
    bad.head = bad.begin + 17; // more than the ring holds
    TEST_ASSERT_FALSE(bad.valid());
    bad = r;
    bad.flushed = bad.head + 1; // flushed past the head
    TEST_ASSERT_FALSE(bad.valid());
    bad = r;
    bad.begin = 3; // begin past flushed
    TEST_ASSERT_FALSE(bad.valid());
    bad = r;
    bad.magic ^= 1;
    TEST_ASSERT_FALSE(bad.valid());
    bad = r;
    bad.head = 0xFFFFFFFF; // behind begin, modulo 2^32
    TEST_ASSERT_FALSE(bad.valid());

    auto odd = fresh<6144>();
    odd.head = static_cast<uint32_t>(line_ring<6144>::modulus); // past the modulus
    TEST_ASSERT_FALSE(odd.valid());
}

TEST_CASE("line_ring rejects a ring of another size", "[persistent_log]") {
    // After a firmware update that changes the ring's size, the memory holds a valid ring of the
    // old size. Its indices are in order and span fewer bytes than either size.
    auto small = fresh<16>();
    small.append("0123456789");
    TEST_ASSERT_TRUE(small.valid());
    line_ring<32> large;
    std::memset(&large, 0xA5, sizeof large);
    std::memcpy(&large, &small, sizeof small);
    TEST_ASSERT_FALSE(large.valid());
    // Start resets a ring that isn't valid, so the old ring's bytes are never flushed.
    large.reset();
    TEST_ASSERT_TRUE(large.valid());
    TEST_ASSERT_EQUAL_size_t(0, large.unflushed());
    TEST_ASSERT_EQUAL_size_t(0, large.tail(100).size());

    // The same holds for a larger ring read as a smaller one.
    auto big = fresh<32>();
    big.append("0123456789");
    TEST_ASSERT_TRUE(big.valid());
    line_ring<16> little;
    std::memcpy(&little, &big, sizeof little);
    TEST_ASSERT_FALSE(little.valid());
    little.reset();
    TEST_ASSERT_TRUE(little.valid());
    TEST_ASSERT_EQUAL_size_t(0, little.unflushed());
}

TEST_CASE("line_ring never gives more than it holds, whatever its fields", "[persistent_log]") {
    // As after a power-on, the fields hold whatever was in memory, even when the magic is correct.
    line_ring<16> r;
    uint32_t x = 777;
    auto next = [&x] {
        x = x * 1664525 + 1013904223;
        return x;
    };
    for (int round = 0; round < 200; ++round) {
        std::memset(r.data, 'g', sizeof r.data);
        r.magic = round % 2 == 0 ? line_ring<16>::ring_magic : next();
        r.begin = next();
        r.flushed = next();
        r.head = next();
        r.dropped = next();
        r.dropped_at_flush = next();
        if (round % 4 == 0) {
            // The indices are in order, but they span more than the ring holds.
            r.begin = r.flushed = next() % 1000;
            r.head = r.flushed + 17 + next() % 1000;
        }
        TEST_ASSERT_FALSE(r.valid());
        TEST_ASSERT_TRUE(r.pending().size() <= 16);
        TEST_ASSERT_TRUE(r.unflushed() > 16 || r.pending().size() == r.unflushed());
        TEST_ASSERT_TRUE(r.tail(100).size() <= 16);
    }
}

TEST_CASE("line_ring end_line ends a partial last line", "[persistent_log]") {
    // An empty ring has no line to end.
    auto r = fresh<16>();
    r.end_line();
    TEST_ASSERT_EQUAL_UINT32(0, r.head);

    // The last line has already ended.
    r.append("one\n");
    r.end_line();
    assert_same("one\n", str(r.pending()));

    // The ring ends partway through a line, as a reset between two appends of one line leaves it.
    r.append("tw");
    r.end_line();
    assert_same("one\ntw\n", str(r.pending()));
    r.end_line();
    assert_same("one\ntw\n", str(r.pending()));

    // The ring ends partway through a line that is already flushed. The newline is still added.
    auto f = fresh<16>();
    f.append("one\nthr");
    f.mark_flushed(f.pending());
    f.end_line();
    assert_same("\n", str(f.pending()));
    assert_same("one\nthr\n", str(f.tail(100)));

    // The partial line wraps across the end of `data`.
    auto w = fresh<16>();
    w.append("0123456789abcd\n");
    w.mark_flushed(w.pending());
    w.append("xy");
    w.end_line();
    assert_same("xy\n", str(w.pending()));
    TEST_ASSERT_TRUE(w.valid());
}

TEST_CASE(
    "line_ring gives what's appended as pending until it's marked flushed",
    "[persistent_log]"
) {
    auto r = fresh<16>();
    r.append("one\n");
    r.append("two\n");
    auto p = r.pending();
    assert_same("one\ntwo\n", str(p));
    TEST_ASSERT_EQUAL_UINT32(8, p.end);

    r.append("three\n");
    r.mark_flushed(p);
    assert_same("three\n", str(r.pending()));

    // The pending bytes wrap across the end of `data`, so they are in two spans.
    r.append("four\n");
    p = r.pending();
    TEST_ASSERT_FALSE(p.second.empty());
    assert_same("three\nfour\n", str(p));
    r.mark_flushed(p);
    TEST_ASSERT_EQUAL_size_t(0, r.pending().size());
    TEST_ASSERT_EQUAL_UINT32(0, r.dropped);
    TEST_ASSERT_TRUE(r.valid());
}

TEST_CASE(
    "line_ring drops and counts the oldest unflushed bytes when it overruns",
    "[persistent_log]"
) {
    auto r = fresh<16>();
    r.append("0123456789");
    r.append("abcdefghij");
    assert_same("456789abcdefghij", str(r.pending()));
    TEST_ASSERT_EQUAL_UINT32(4, r.dropped);
    TEST_ASSERT_TRUE(r.valid());

    // Flushed bytes are overwritten without being counted as dropped.
    r.mark_flushed(r.pending());
    r.append("klmnop");
    assert_same("klmnop", str(r.pending()));
    TEST_ASSERT_EQUAL_UINT32(4, r.dropped);
}

TEST_CASE("line_ring counts the bytes lost since the last flush", "[persistent_log]") {
    auto r = fresh<16>();
    TEST_ASSERT_EQUAL_UINT32(0, r.pending().lost);
    r.append("0123456789");
    r.append("abcdefghij"); // overruns by 4
    auto p = r.pending();
    TEST_ASSERT_EQUAL_UINT32(4, p.lost);
    r.mark_flushed(p);
    TEST_ASSERT_EQUAL_UINT32(0, r.pending().lost);
    TEST_ASSERT_EQUAL_UINT32(4, r.dropped);

    // Overwriting flushed bytes isn't counted as a loss.
    r.append("klmnopqrstuvw");
    TEST_ASSERT_EQUAL_UINT32(0, r.pending().lost);
    // Losses add up until a flush. Here, an overrun is followed by text longer than the ring.
    r.append("0123456789"); // overruns by 7
    TEST_ASSERT_EQUAL_UINT32(7, r.pending().lost);
    r.append("ABCDEFGHIJKLMNOPQRST"); // 4 bytes cut off, and the 16 pending bytes released
    TEST_ASSERT_EQUAL_UINT32(7 + 4 + 16, r.pending().lost);
    r.mark_flushed(r.pending());
    TEST_ASSERT_EQUAL_UINT32(0, r.pending().lost);

    // A reset clears the losses.
    r.append("0123456789abcdefghij");
    TEST_ASSERT_TRUE(r.pending().lost > 0);
    r.reset();
    TEST_ASSERT_EQUAL_UINT32(0, r.pending().lost);
    TEST_ASSERT_TRUE(r.valid());
}

TEST_CASE(
    "line_ring counts as lost only what an overrun took past a flush's bytes",
    "[persistent_log]"
) {
    // The flush had already copied the bytes that an overrun then released, so they were flushed,
    // not lost.
    auto r = fresh<16>();
    r.append("0123456789");
    auto p = r.pending();
    r.append("abcdefghijklmn"); // releases 8 of p's 10 bytes
    r.mark_flushed(p);
    TEST_ASSERT_EQUAL_UINT32(0, r.pending().lost);
    assert_same("abcdefghijklmn", str(r.pending()));

    // Bytes released past the end of the flush were never written, so they are lost.
    auto s = fresh<16>();
    s.append("0123456789");
    auto q = s.pending();
    s.append("abcdefghij"); // releases 4 of q's bytes
    s.append("klmnopqrst"); // releases q's other 6, and 4 past it
    s.mark_flushed(q);
    TEST_ASSERT_EQUAL_UINT32(4, s.pending().lost);
    assert_same("efghijklmnopqrst", str(s.pending()));

    // A loss that happened before the flush took its bytes is reported by that flush, and isn't
    // reported again.
    auto t = fresh<16>();
    t.append("0123456789");
    t.append("abcdefghij"); // overruns by 4
    auto u = t.pending();
    TEST_ASSERT_EQUAL_UINT32(4, u.lost);
    t.append("klm"); // releases 3 of u's bytes
    t.mark_flushed(u);
    TEST_ASSERT_EQUAL_UINT32(0, t.pending().lost);
    assert_same("klm", str(t.pending()));
}

TEST_CASE("line_ring keeps the end of text longer than itself", "[persistent_log]") {
    auto r = fresh<16>();
    r.append("xyz");
    r.append("0123456789abcdefghij"); // 20 bytes
    assert_same("456789abcdefghij", str(r.pending()));
    TEST_ASSERT_EQUAL_UINT32(3 + 4, r.dropped);
    TEST_ASSERT_TRUE(r.valid());
}

TEST_CASE("line_ring mark_flushed ignores a stale or future index", "[persistent_log]") {
    auto r = fresh<16>();
    r.append("0123456789");
    auto p = r.pending();
    // The writer copies p and writes it out. While it does, appends overrun past p.end.
    r.append("abcdefghij");
    r.append("klmnopqrst");
    uint32_t flushed = r.flushed;
    TEST_ASSERT_EQUAL_UINT32(14, flushed);
    r.mark_flushed(p);
    TEST_ASSERT_EQUAL_UINT32(flushed, r.flushed);
    r.mark_flushed(ending_at(r.head + 1));
    TEST_ASSERT_EQUAL_UINT32(flushed, r.flushed);
    r.mark_flushed(ending_at(0xFFFFFFFF));
    TEST_ASSERT_EQUAL_UINT32(flushed, r.flushed);
    r.mark_flushed(r.pending());
    TEST_ASSERT_EQUAL_size_t(0, r.unflushed());
    TEST_ASSERT_TRUE(r.valid());
}

TEST_CASE("line_ring mark_flushed moves past bytes an overrun released", "[persistent_log]") {
    auto r = fresh<16>();
    r.append("0123456789");
    auto p = r.pending();
    r.append("abcdefghijklmn"); // releases 8 of p's 10 bytes
    TEST_ASSERT_EQUAL_UINT32(8, r.flushed);
    r.mark_flushed(p);
    TEST_ASSERT_EQUAL_UINT32(10, r.flushed);
    assert_same("abcdefghijklmn", str(r.pending()));
}

TEST_CASE("line_ring tail begins at the start of a line", "[persistent_log]") {
    auto r = fresh<64>();
    r.append("one\ntwo\nthree\n"); // 14 bytes
    assert_same("one\ntwo\nthree\n", str(r.tail(100)));
    assert_same("one\ntwo\nthree\n", str(r.tail(14)));
    assert_same("two\nthree\n", str(r.tail(10)));
    assert_same("three\n", str(r.tail(9)));
    assert_same("", str(r.tail(4)));
    assert_same("", str(r.tail(0)));
    // Flushed bytes are still included in the tail.
    r.mark_flushed(r.pending());
    assert_same("two\nthree\n", str(r.tail(10)));
}

TEST_CASE(
    "line_ring tail of a wrapped ring leaves out its partial first line",
    "[persistent_log]"
) {
    auto r = fresh<16>();
    r.append("aaaa\n");
    r.append("bbbb\n");
    r.append("cccc\n");
    r.append("dddd\n"); // 20 bytes, so the oldest 4 are gone
    assert_same("bbbb\ncccc\ndddd\n", str(r.tail(100)));
    assert_same("cccc\ndddd\n", str(r.tail(10)));
    // The tail wraps across the end of `data`.
    r.append("ee\n");
    assert_same("cccc\ndddd\nee\n", str(r.tail(100)));
}

TEST_CASE(
    "line_ring tail begins at an entry, past the lines that continue one",
    "[persistent_log]"
) {
    auto r = fresh<64>();
    r.append("1 one\n\ta\n\tb\n2 two\n\tc\n"); // 21 bytes
    assert_same("1 one\n\ta\n\tb\n2 two\n\tc\n", str(r.tail(21)));
    // The requested size reaches back to the start of a line that continues an entry.
    assert_same("2 two\n\tc\n", str(r.tail(15)));
    // The requested size reaches back to partway through a line, and more lines of its entry
    // follow it.
    assert_same("2 two\n\tc\n", str(r.tail(14)));
    assert_same("2 two\n\tc\n", str(r.tail(13)));
    // When the requested size holds only an entry's continuation lines, the tail is empty.
    assert_same("", str(r.tail(3)));
    assert_same("", str(r.tail(5)));

    // This ring wraps, and its oldest bytes are partway through an entry.
    auto w = fresh<16>();
    w.append("1 a\n\tb\n");
    w.append("2 c\n\td\n");
    w.append("3 e\n"); // 18 bytes, so the oldest 2 are gone
    assert_same("2 c\n\td\n3 e\n", str(w.tail(100)));
    // This ring wraps, and its oldest byte is partway through a continuation line, with another
    // continuation line after it.
    auto v = fresh<16>();
    v.append("1 a\n\tbb\n\tc\n");
    v.append("2 d\n");
    v.append("3 efg\n"); // 21 bytes, so the oldest 5 are gone
    assert_same("2 d\n3 efg\n", str(v.tail(100)));
}

TEST_CASE("line_ring indices wrap at the modulus", "[persistent_log]") {
    auto r = fresh<6144>();
    uint32_t near = static_cast<uint32_t>(line_ring<6144>::modulus - 5);
    r.begin = near;
    r.flushed = near;
    r.head = near;
    TEST_ASSERT_TRUE(r.valid());
    r.append("ab\n");
    r.append("0123456789\n");
    TEST_ASSERT_TRUE(r.valid());
    TEST_ASSERT_EQUAL_UINT32(9, r.head);
    assert_same("ab\n0123456789\n", str(r.pending()));
    assert_same("0123456789\n", str(r.tail(11)));
    // `begin` was set by hand, not by reset(), so the oldest line may be partial.
    assert_same("0123456789\n", str(r.tail(100)));
    r.mark_flushed(r.pending());
    TEST_ASSERT_EQUAL_size_t(0, r.unflushed());

    auto p2 = fresh<16>();
    p2.begin = p2.flushed = p2.head = 0xFFFFFFFC;
    p2.append("abcdefgh");
    TEST_ASSERT_TRUE(p2.valid());
    TEST_ASSERT_EQUAL_UINT32(4, p2.head);
    assert_same("abcdefgh", str(p2.pending()));
}

// The append is stopped by an exception thrown from its step hook.
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("line_ring is consistent after an append stopped at any step", "[persistent_log]") {
    // Each case gives the ring's contents, how many of those bytes are flushed, and the text to
    // append. Together, the cases reach every step, with and without dropping bytes, and across the
    // end of `data`.
    struct scenario {
        const char* before;
        size_t flush;
        const char* text;
    };
    int reached[5] = {};
    for (scenario s : {
             scenario{"one\ntwo\n", 0, "abc\n"},
             scenario{"one\ntwo\n", 8, "abcdefghijk\n"},
             scenario{"one\ntwo\nthree\n", 4, "four\nfive\n"},
             scenario{"one\ntwo\nthree\n", 0, "four\nfive\n"},
             scenario{"one\n", 0, "0123456789abcdefghij\n"},
         }) {
        for (auto at : {
                 append_step::counted_dropped,
                 append_step::released_unflushed,
                 append_step::released_oldest,
                 append_step::copied_part,
                 append_step::copied,
             }) {
            auto r = fresh<16>();
            r.append("zzzzzzzzzzzzz\n"); // so that the appends below don't start at data[0]
            r.mark_flushed(r.pending());
            r.append(std::string_view(s.before).substr(0, s.flush));
            r.mark_flushed(r.pending());
            r.append(std::string_view(s.before).substr(s.flush));
            std::string history = std::string("zzzzzzzzzzzzz\n") + s.before;
            std::string pending_before = str(r.pending());
            uint32_t dropped_before = r.dropped;

            bool stopped = false;
            try {
                r.append(s.text, [&](append_step step) {
                    if (step == at) {
                        throw stop{};
                    }
                });
            } catch (const stop&) {
                stopped = true;
            }
            if (!stopped) {
                continue; // this append has no such step
            }
            ++reached[static_cast<int>(at)];

            // Check the ring as the next boot would find it. It is valid, and it holds what it
            // held before, or the newest part of that. None of the new text appears, completely or
            // in part.
            TEST_ASSERT_TRUE(r.valid());
            std::string pending = str(r.pending());
            TEST_ASSERT_TRUE(std::string_view(pending_before).ends_with(pending));
            std::string tail = str(r.tail(16));
            TEST_ASSERT_TRUE(std::string_view(history).ends_with(tail));
            // The tail also begins at the start of a line.
            size_t from = history.size() - tail.size();
            TEST_ASSERT_TRUE(tail.empty() || from == 0 || history[from - 1] == '\n');
            // Bytes released from pending are counted as dropped.
            TEST_ASSERT_TRUE(r.dropped - dropped_before >= pending_before.size() - pending.size());

            // The ring also accepts appends again.
            r.append("next\n");
            TEST_ASSERT_TRUE(r.valid());
            TEST_ASSERT_TRUE((pending + "next\n").ends_with(str(r.pending())));
            TEST_ASSERT_TRUE(str(r.pending()).ends_with("next\n"));
            // When the stopped append had released everything, `begin` is no longer where reset()
            // put it, so the tail can't tell that "next" starts a line.
            std::string tail_after = str(r.tail(16));
            TEST_ASSERT_TRUE(
                tail_after.ends_with("next\n") || (tail.empty() && tail_after.empty())
            );
        }
    }
    for (int n : reached) {
        TEST_ASSERT_GREATER_THAN(0, n);
    }
}
#endif
