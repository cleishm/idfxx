// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_font
// Pure font-model arithmetic — runs everywhere, including QEMU.

#include "idfxx/font"
#include "unity.h"

#include <cstddef>
#include <type_traits>

using namespace idfxx::font;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::is_trivially_copyable_v<mono_font>);

namespace {

// A two-glyph 5x8 test font: 'A' fully inked, 'B' blank.
constexpr uint8_t test_bitmap[16] = {
    0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, // 'A'
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 'B'
};
constexpr mono_font test_font{.width = 5, .height = 8, .first_char = 'A', .last_char = 'B', .bitmap = test_bitmap};

} // namespace

// Glyph rows are packed into ceil(width / 8) bytes.
static_assert(test_font.bytes_per_row() == 1);
static_assert(test_font.bytes_per_glyph() == 8);
static_assert(mono_font{.width = 8, .height = 16, .first_char = ' ', .last_char = '~', .bitmap = nullptr}
                  .bytes_per_row() == 1);
static_assert(mono_font{.width = 9, .height = 8, .first_char = ' ', .last_char = '~', .bitmap = nullptr}
                  .bytes_per_row() == 2);
static_assert(mono_font{.width = 16, .height = 8, .first_char = ' ', .last_char = '~', .bitmap = nullptr}
                  .bytes_per_glyph() == 16);

// The glyph range is inclusive at both ends.
static_assert(test_font.contains('A'));
static_assert(test_font.contains('B'));
static_assert(!test_font.contains('C'));
static_assert(!test_font.contains('\n'));

// Glyphs are stored consecutively.
static_assert(test_font.glyph('A') == test_bitmap);
static_assert(test_font.glyph('B') == test_bitmap + 8);

// ink_at decodes the MSB-first packed rows.
static_assert(test_font.ink_at('A', 0, 0));
static_assert(test_font.ink_at('A', 4, 7));
static_assert(!test_font.ink_at('B', 0, 0));

// Every character advances one cell, including ones without a glyph.
static_assert(test_font.advance('A') == 5);
static_assert(test_font.advance('\x01') == 5);

// text_width sums advances and applies the scale factor.
static_assert(text_width(test_font, "") == 0);
static_assert(text_width(test_font, "AB") == 10);
static_assert(text_width(test_font, "AB", 2) == 20);
static_assert(text_width(test_font, "A\x01") == 10);
static_assert(text_width(test_font, "AB", 0) == 10); // scale 0 is treated as 1

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("font mono_font reports glyph geometry", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(1, test_font.bytes_per_row());
    TEST_ASSERT_EQUAL(8, test_font.bytes_per_glyph());
    TEST_ASSERT_TRUE(test_font.contains('A'));
    TEST_ASSERT_FALSE(test_font.contains('z'));
    TEST_ASSERT_EQUAL_PTR(test_bitmap + 8, test_font.glyph('B'));
}

TEST_CASE("font text_width scales with length and factor", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(0, text_width(test_font, ""));
    TEST_ASSERT_EQUAL(15, text_width(test_font, "ABA"));
    TEST_ASSERT_EQUAL(30, text_width(test_font, "ABA", 2));
}
