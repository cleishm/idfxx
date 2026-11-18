// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx UTF-8 decoding
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/utf8.hpp"
#include "unity.h"

#include <array>
#include <cstddef>
#include <ranges>
#include <string_view>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::ranges::forward_range<utf8_view>);
static_assert(std::ranges::view<utf8_view>);
static_assert(std::ranges::borrowed_range<utf8_view>);

namespace {

// Returns the code points of text, padded with zeros, plus how many there were.
template<size_t N = 8>
struct decoded {
    std::array<char32_t, N> cps{};
    size_t count = 0;
};

template<size_t N = 8>
constexpr decoded<N> decode(std::string_view text) {
    decoded<N> out;
    for (char32_t cp : utf8_view(text)) {
        if (out.count < N) {
            out.cps[out.count] = cp;
        }
        ++out.count;
    }
    return out;
}

constexpr char32_t fffd = utf8_view::replacement_character;

} // namespace

// Well-formed sequences of every length.
static_assert(decode("").count == 0);
static_assert(decode("A").count == 1 && decode("A").cps[0] == U'A');
static_assert(decode("caf\xC3\xA9").count == 4 && decode("caf\xC3\xA9").cps[3] == U'\u00E9');
static_assert(decode("\xE2\x80\x99").count == 1 && decode("\xE2\x80\x99").cps[0] == U'\u2019');
static_assert(decode("\xF0\x9F\x98\x80").count == 1 && decode("\xF0\x9F\x98\x80").cps[0] == U'\U0001F600');
static_assert(decode("\xF4\x8F\xBF\xBF").cps[0] == U'\U0010FFFF');

// Each maximal ill-formed subsequence becomes one U+FFFD.
static_assert(decode("\x80").count == 1 && decode("\x80").cps[0] == fffd);                    // stray continuation
static_assert(decode("\xE2\x80").count == 1 && decode("\xE2\x80").cps[0] == fffd);            // truncated at end
static_assert(decode("\xE2\x80" "A").count == 2 && decode("\xE2\x80" "A").cps[1] == U'A');    // truncated mid-text
static_assert(decode("\xC0\xAF").count == 2 && decode("\xC0\xAF").cps[1] == fffd);            // overlong lead
static_assert(decode("\xE0\x80\x80").count == 3);                                             // overlong 3-byte
static_assert(decode("\xED\xA0\x80").count == 3 && decode("\xED\xA0\x80").cps[0] == fffd);    // surrogate
static_assert(decode("\xF4\x90\x80\x80").count == 4);                                         // above U+10FFFF
static_assert(decode("\xFF" "A").count == 2 && decode("\xFF" "A").cps[1] == U'A');            // invalid byte

// Iterators expose the byte offset of each code point.
static_assert([] {
    auto it = utf8_view("a\xC3\xA9z").begin();
    const size_t first = it.offset();
    ++it;
    const size_t second = it.offset();
    ++it;
    return first == 0 && second == 1 && it.offset() == 3;
}());

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("utf8_view decodes text and replaces malformed input", "[idfxx][utf8]") {
    const auto ok = decode("A\xC3\xA9\xE2\x80\x99");
    TEST_ASSERT_EQUAL(3, ok.count);
    TEST_ASSERT_EQUAL_HEX32(U'\u00E9', ok.cps[1]);
    TEST_ASSERT_EQUAL_HEX32(U'\u2019', ok.cps[2]);

    const auto bad = decode("\xE2\x80" "A\x80");
    TEST_ASSERT_EQUAL(3, bad.count);
    TEST_ASSERT_EQUAL_HEX32(fffd, bad.cps[0]);
    TEST_ASSERT_EQUAL_HEX32(U'A', bad.cps[1]);
    TEST_ASSERT_EQUAL_HEX32(fffd, bad.cps[2]);
}
