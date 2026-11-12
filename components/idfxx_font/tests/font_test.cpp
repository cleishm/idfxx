// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_font
// Pure font-model arithmetic — runs everywhere, including QEMU.

#include "idfxx/font"
#include "unity.h"

#include <cstddef>
#include <string_view>
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

// -----------------------------------------------------------------------------
// proportional_font
// -----------------------------------------------------------------------------

static_assert(std::is_trivially_copyable_v<proportional_font>);

namespace {

// Three glyphs over two ranges: 'A'-'B' and U+2019, with '?' absent.
constexpr uint8_t prop_bitmap[] = {
    0xA0, 0x40,       // 'A': 3x2
    0x80, 0x80, 0x80, // 'B': 1x3
    0xC0,             // U+2019: 2x1
};
constexpr glyph_metrics prop_glyphs[] = {
    {.bitmap_offset = 0, .width = 3, .height = 2, .advance = 4, .x_offset = 0, .y_offset = -2},
    {.bitmap_offset = 2, .width = 1, .height = 3, .advance = 2, .x_offset = 0, .y_offset = -3},
    {.bitmap_offset = 5, .width = 2, .height = 1, .advance = 3, .x_offset = 1, .y_offset = -3},
};
constexpr glyph_range prop_ranges[] = {
    {.first = U'A', .last = U'B', .first_glyph = 0},
    {.first = U'\u2019', .last = U'\u2019', .first_glyph = 2},
};
constexpr proportional_font prop_font{
    .ascent = 3,
    .descent = 1,
    .line_height = 5,
    .ranges = prop_ranges,
    .glyphs = prop_glyphs,
    .replacement = nullptr,
    .bitmap = prop_bitmap,
};
constexpr proportional_font prop_font_with_replacement{
    .ascent = 3,
    .descent = 1,
    .line_height = 5,
    .ranges = prop_ranges,
    .glyphs = prop_glyphs,
    .replacement = &prop_glyphs[1],
    .bitmap = prop_bitmap,
};

} // namespace

// Lookup walks the ranges: hits inside each run, misses between and outside them.
static_assert(prop_font.find(U'A') == &prop_glyphs[0]);
static_assert(prop_font.find(U'B') == &prop_glyphs[1]);
static_assert(prop_font.find(U'\u2019') == &prop_glyphs[2]);
static_assert(prop_font.find(U'@') == nullptr);
static_assert(prop_font.find(U'C') == nullptr);
static_assert(prop_font.find(U'\U0010FFFF') == nullptr);
static_assert(prop_font.contains(U'A') && !prop_font.contains(U'?'));
static_assert(proportional_font{}.find(U'A') == nullptr); // no ranges at all

// resolve falls back to the replacement glyph; contains does not.
static_assert(prop_font.resolve(U'?') == nullptr);
static_assert(prop_font_with_replacement.resolve(U'?') == &prop_glyphs[1]);
static_assert(!prop_font_with_replacement.contains(U'?'));

// Advances come from the drawn glyph; skipped code points take no space.
static_assert(prop_font.advance(U'A') == 4);
static_assert(prop_font.advance(U'?') == 0);
static_assert(prop_font_with_replacement.advance(U'?') == 2);

// Bitmaps are located by offset and decoded MSB-first, rows padded to bytes.
static_assert(prop_glyphs[0].bytes_per_row() == 1);
static_assert(
    glyph_metrics{.bitmap_offset = 0, .width = 9, .height = 1, .advance = 9, .x_offset = 0, .y_offset = 0}
        .bytes_per_row() == 2
);
static_assert(prop_font.glyph_bitmap(prop_glyphs[1]) == prop_bitmap + 2);
static_assert(prop_font.ink_at(prop_glyphs[0], 0, 0) && !prop_font.ink_at(prop_glyphs[0], 1, 0));
static_assert(prop_font.ink_at(prop_glyphs[0], 2, 0) && prop_font.ink_at(prop_glyphs[0], 1, 1));

// text_width decodes UTF-8 and sums advances.
static_assert(text_width(prop_font, "") == 0);
static_assert(text_width(prop_font, "AB") == 6);
static_assert(text_width(prop_font, "A\xE2\x80\x99" "B") == 9);
static_assert(text_width(prop_font, "A?B") == 6);
static_assert(text_width(prop_font_with_replacement, "A?B") == 8);
static_assert(text_width(prop_font_with_replacement, "A\xFF") == 6); // malformed byte -> replacement

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

TEST_CASE("font proportional_font looks up glyphs and measures text", "[idfxx][font]") {
    TEST_ASSERT_EQUAL_PTR(&prop_glyphs[2], prop_font.find(U'\u2019'));
    TEST_ASSERT_NULL(prop_font.find(U'Z'));
    TEST_ASSERT_EQUAL_PTR(&prop_glyphs[1], prop_font_with_replacement.resolve(U'Z'));
    TEST_ASSERT_EQUAL(9, text_width(prop_font, "A\xE2\x80\x99" "B"));
    TEST_ASSERT_EQUAL(8, text_width(prop_font_with_replacement, "A?B"));
}
