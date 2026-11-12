// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_font
// Pure font-model arithmetic — runs everywhere, including QEMU.

#include "idfxx/font"
#include "unity.h"

#include <cstddef>
#include <iterator>
#include <ranges>
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

// Six glyphs over five ranges: ' ', '-', 'A'-'B', U+00AD and U+2019, with
// '?' absent. The soft hyphen has a glyph and an advance of its own, which
// text measurement and line breaking must ignore.
constexpr uint8_t prop_bitmap[] = {
    0xA0, 0x40,       // 'A': 3x2
    0x80, 0x80, 0x80, // 'B': 1x3
    0xC0,             // U+2019: 2x1
    0xC0,             // '-': 2x1
    0xF8,             // U+00AD: 5x1
};
constexpr glyph_metrics prop_glyphs[] = {
    {.bitmap_offset = 0, .width = 3, .height = 2, .advance = 4, .x_offset = 0, .y_offset = -2}, // 'A'
    {.bitmap_offset = 2, .width = 1, .height = 3, .advance = 2, .x_offset = 0, .y_offset = -3}, // 'B'
    {.bitmap_offset = 5, .width = 2, .height = 1, .advance = 3, .x_offset = 1, .y_offset = -3}, // U+2019
    {.bitmap_offset = 0, .width = 0, .height = 0, .advance = 1, .x_offset = 0, .y_offset = 0},  // ' '
    {.bitmap_offset = 6, .width = 2, .height = 1, .advance = 2, .x_offset = 0, .y_offset = -1}, // '-'
    {.bitmap_offset = 7, .width = 5, .height = 1, .advance = 5, .x_offset = 0, .y_offset = -1}, // U+00AD
};
constexpr glyph_range prop_ranges[] = {
    {.first = U' ', .last = U' ', .first_glyph = 3},
    {.first = U'-', .last = U'-', .first_glyph = 4},
    {.first = U'A', .last = U'B', .first_glyph = 0},
    {.first = U'\u00AD', .last = U'\u00AD', .first_glyph = 5},
    {.first = U'\u2019', .last = U'\u2019', .first_glyph = 2},
};
// The same glyphs without the soft hyphen, which then resolves to the
// replacement glyph ('A').
constexpr glyph_range prop_ranges_no_soft_hyphen[] = {
    {.first = U' ', .last = U' ', .first_glyph = 3},
    {.first = U'-', .last = U'-', .first_glyph = 4},
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
constexpr proportional_font prop_font_with_replacement = [] {
    proportional_font font = prop_font;
    font.replacement = &prop_glyphs[1];
    return font;
}();
constexpr proportional_font prop_font_replaced_soft_hyphen = [] {
    proportional_font font = prop_font;
    font.ranges = prop_ranges_no_soft_hyphen;
    font.replacement = &prop_glyphs[0];
    return font;
}();

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

// A soft hyphen is invisible and zero-width, whether the font has a glyph for
// it or would draw the replacement.
static_assert(prop_font.advance(U'\u00AD') == 5);
static_assert(prop_font_replaced_soft_hyphen.advance(U'\u00AD') == 4);
static_assert(text_width(prop_font, "A\xC2\xAD" "B") == 6);
static_assert(text_width(prop_font_replaced_soft_hyphen, "A\xC2\xAD" "B") == 6);
static_assert(text_width(prop_font, "\xC2\xAD") == 0);

// -----------------------------------------------------------------------------
// break_lines
// -----------------------------------------------------------------------------

static_assert(std::ranges::forward_range<line_break_view<proportional_font>>);
static_assert(std::ranges::view<line_break_view<proportional_font>>);
static_assert(std::ranges::borrowed_range<line_break_view<proportional_font>>);
static_assert(std::ranges::forward_range<line_break_view<mono_font>>);
static_assert(std::ranges::view<line_break_view<mono_font>>);
static_assert(std::ranges::borrowed_range<line_break_view<mono_font>>);
static_assert(std::is_same_v<std::ranges::range_value_t<line_break_view<mono_font>>, text_line>);

namespace {

// True if breaking text in font at max_width (and scale, for a mono font) yields exactly the
// expected lines, each as wide as its text measures, plus the drawn hyphen when hyphenated.
template<typename Font, size_t N>
constexpr bool breaks_into(
    const Font& font, std::string_view text, size_t max_width, const text_line (&expected)[N], unsigned scale = 1
) {
    constexpr bool proportional = std::is_same_v<Font, proportional_font>;
    line_break_view<Font> lines;
    if constexpr (proportional) {
        lines = break_lines(font, text, max_width);
    } else {
        lines = break_lines(font, text, max_width, scale);
    }
    size_t i = 0;
    for (const text_line& line : lines) {
        size_t width;
        if constexpr (proportional) {
            width = text_width(font, line.text) + (line.hyphenated ? font.advance(U'-') : 0);
        } else {
            width = text_width(font, line.text, scale);
        }
        if (i >= N || line != expected[i] || line.width != width) {
            return false;
        }
        ++i;
    }
    return i == N;
}

// Returns the byte offset of a line's text within the input.
constexpr size_t line_offset(std::string_view text, size_t max_width, size_t n) {
    const text_line line = *std::ranges::next(break_lines(prop_font, text, max_width).begin(), n);
    return static_cast<size_t>(line.text.data() - text.data());
}

// Advances: 'A' 4, 'B' 2, ' ' 1, '-' 2, U+2019 3; U+00AD 0 in text (5 in the font).

} // namespace

// Empty text yields no lines; text that fits is one line.
static_assert(break_lines(prop_font, "", 10).empty());
static_assert(breaks_into(prop_font, "AB AB", 20, {{"AB AB", 13, false}}));

// Lines wrap at spaces, taking as many words as fit.
static_assert(breaks_into(prop_font, "AB AB AB", 13, {{"AB AB", 13, false}, {"AB", 6, false}}));
static_assert(breaks_into(prop_font, "AB AB AB", 12, {{"AB", 6, false}, {"AB", 6, false}, {"AB", 6, false}}));

// All spaces at a wrap point are dropped from both lines.
static_assert(breaks_into(prop_font, "AB   AB", 8, {{"AB", 6, false}, {"AB", 6, false}}));
static_assert(line_offset("AB   AB", 8, 1) == 5);

// Spaces never cause a wrap: a word filling the line exactly may be followed by spaces.
static_assert(breaks_into(prop_font, "AB AB", 6, {{"AB", 6, false}, {"AB", 6, false}}));
static_assert(breaks_into(prop_font, "AB   ", 6, {{"AB", 6, false}}));

// Leading spaces are kept as indentation at the start of the text and after
// a hard break, but dropped when they do not fit with the first character.
static_assert(breaks_into(prop_font, "  AB", 10, {{"  AB", 8, false}}));
static_assert(breaks_into(prop_font, "AB\n  AB", 10, {{"AB", 6, false}, {"  AB", 8, false}}));
static_assert(breaks_into(prop_font, "  AB", 7, {{"  A", 6, false}, {"B", 2, false}}));
static_assert(breaks_into(prop_font, "    A", 6, {{"A", 4, false}}));

// A line holding only spaces is empty.
static_assert(breaks_into(prop_font, "   ", 10, {{"", 0, false}}));
static_assert(breaks_into(prop_font, "  \nA", 10, {{"", 0, false}, {"A", 4, false}}));

// Hard breaks: '\n' and "\r\n" end a line; trailing spaces before them are dropped.
static_assert(breaks_into(prop_font, "AB\nB", 20, {{"AB", 6, false}, {"B", 2, false}}));
static_assert(breaks_into(prop_font, "AB\r\nB", 20, {{"AB", 6, false}, {"B", 2, false}}));
static_assert(breaks_into(prop_font, "AB  \nB", 20, {{"AB", 6, false}, {"B", 2, false}}));
static_assert(breaks_into(prop_font, "AB   \nAB", 6, {{"AB", 6, false}, {"AB", 6, false}}));
static_assert(breaks_into(prop_font, "A\rB", 20, {{"A\rB", 6, false}})); // a lone '\r' is not a break

// A trailing '\n' ends the last line; consecutive ones yield empty lines.
static_assert(breaks_into(prop_font, "AB\n", 20, {{"AB", 6, false}}));
static_assert(breaks_into(prop_font, "\n", 20, {{"", 0, false}}));
static_assert(breaks_into(prop_font, "A\n\nB", 20, {{"A", 4, false}, {"", 0, false}, {"B", 2, false}}));
static_assert(breaks_into(prop_font, "A\r\n\r\n", 20, {{"A", 4, false}, {"", 0, false}}));

// A '-' after a non-space character is a break opportunity, staying on the first line.
static_assert(breaks_into(prop_font, "AB-AB", 9, {{"AB-", 8, false}, {"AB", 6, false}}));
static_assert(breaks_into(prop_font, "AB-AB", 14, {{"AB-AB", 14, false}}));
// ...but not a '-' after a space, or at the start of the text.
static_assert(breaks_into(prop_font, "AB -AB", 10, {{"AB", 6, false}, {"-AB", 8, false}}));
static_assert(breaks_into(prop_font, "-AB", 6, {{"-A", 6, false}, {"B", 2, false}}));
// A soft hyphen before the '-' is invisible and does not make it a break.
static_assert(breaks_into(prop_font, "\xC2\xAD-AB", 6, {{"-A", 6, false}, {"B", 2, false}}));
static_assert(breaks_into(prop_font, "AB \xC2\xAD-AB", 10, {{"AB", 6, false}, {"-AB", 8, false}}));

// A soft hyphen is a break opportunity when the text before it plus a '-' fits.
static_assert(breaks_into(prop_font, "AB\xC2\xAD" "AB", 9, {{"AB", 8, true}, {"AB", 6, false}}));
static_assert(line_offset("AB\xC2\xAD" "AB", 9, 1) == 4);
static_assert(breaks_into(prop_font, "AB\xC2\xAD" "AB", 8, {{"AB", 8, true}, {"AB", 6, false}}));
// When the hyphen would not fit, an earlier opportunity is taken instead...
static_assert(breaks_into(prop_font, "B AA\xC2\xAD" "A", 11, {{"B", 2, false}, {"AA", 10, true}, {"A", 4, false}}));
// ...or the word is split without a hyphen.
static_assert(breaks_into(prop_font, "AB\xC2\xAD" "AB", 7, {{"AB", 6, false}, {"AB", 6, false}}));
// Elsewhere it is invisible, whether the font has a glyph or a replacement for it.
static_assert(breaks_into(prop_font, "A\xC2\xAD" "B", 6, {{"A\xC2\xAD" "B", 6, false}}));
static_assert(breaks_into(prop_font_replaced_soft_hyphen, "A\xC2\xAD" "B", 6, {{"A\xC2\xAD" "B", 6, false}}));
static_assert(breaks_into(prop_font, "AB\xC2\xAD", 6, {{"AB", 6, false}}));

// A word that does not fit on a line by itself is split after the last character that fits.
static_assert(breaks_into(prop_font, "AAAA", 10, {{"AA", 8, false}, {"AA", 8, false}}));
static_assert(breaks_into(prop_font, "B AAAA", 10, {{"B", 2, false}, {"AA", 8, false}, {"AA", 8, false}}));
static_assert(breaks_into(prop_font, "AAA", 3, {{"A", 4, false}, {"A", 4, false}, {"A", 4, false}}));

// A maximum width of 0 puts each character on its own line.
static_assert(breaks_into(prop_font, "AB A", 0, {{"A", 4, false}, {"B", 2, false}, {"A", 4, false}}));

// Splits fall on UTF-8 code point boundaries.
static_assert(breaks_into(
    prop_font,
    "\xE2\x80\x99\xE2\x80\x99\xE2\x80\x99",
    7,
    {{"\xE2\x80\x99\xE2\x80\x99", 6, false}, {"\xE2\x80\x99", 3, false}}
));
static_assert(breaks_into(prop_font, "A\xE2\x80\x99", 0, {{"A", 4, false}, {"\xE2\x80\x99", 3, false}}));
static_assert(line_offset("\xE2\x80\x99\xE2\x80\x99\xE2\x80\x99", 7, 1) == 6);

// Mono fonts break bytes in scaled cells; soft hyphens are ordinary bytes.
static_assert(breaks_into(test_font, "AB AB", 20, {{"AB", 20, false}, {"AB", 20, false}}, 2));
static_assert(breaks_into(test_font, "AB AB", 25, {{"AB AB", 25, false}}, 0)); // scale 0 is treated as 1
static_assert(breaks_into(test_font, "AB-AB", 15, {{"AB-", 15, false}, {"AB", 10, false}}));
// A '-' starting a wrapped line is not a break: no line of just "-".
static_assert(breaks_into(test_font, "ABAB-BABA", 20, {{"ABAB", 20, false}, {"-BAB", 20, false}, {"A", 5, false}}));
static_assert(breaks_into(test_font, "ABAB", 10, {{"AB", 10, false}, {"AB", 10, false}}));
static_assert(breaks_into(test_font, "A\xC2\xAD" "B", 100, {{"A\xC2\xAD" "B", 20, false}}));
static_assert(breaks_into(test_font, "A\n\n  B\n", 100, {{"A", 5, false}, {"", 0, false}, {"  B", 15, false}}));
static_assert(break_lines(test_font, "", 10).empty());

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

TEST_CASE("font break_lines wraps at spaces and drops them", "[idfxx][font]") {
    TEST_ASSERT_TRUE(break_lines(prop_font, "", 10).empty());
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB AB AB", 13, {{"AB AB", 13, false}, {"AB", 6, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB   AB", 8, {{"AB", 6, false}, {"AB", 6, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB AB", 6, {{"AB", 6, false}, {"AB", 6, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB   ", 6, {{"AB", 6, false}}));
}

TEST_CASE("font break_lines keeps indentation", "[idfxx][font]") {
    TEST_ASSERT_TRUE(breaks_into(prop_font, "  AB\n  AB", 10, {{"  AB", 8, false}, {"  AB", 8, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "    A", 6, {{"A", 4, false}}));
}

TEST_CASE("font break_lines honors hard breaks", "[idfxx][font]") {
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB\nB", 20, {{"AB", 6, false}, {"B", 2, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB  \r\nB", 20, {{"AB", 6, false}, {"B", 2, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB\n", 20, {{"AB", 6, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "A\n\nB", 20, {{"A", 4, false}, {"", 0, false}, {"B", 2, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "\n\n", 20, {{"", 0, false}, {"", 0, false}}));
}

TEST_CASE("font break_lines breaks after hyphens", "[idfxx][font]") {
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB-AB", 9, {{"AB-", 8, false}, {"AB", 6, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB -AB", 10, {{"AB", 6, false}, {"-AB", 8, false}}));
}

TEST_CASE("font break_lines hyphenates at soft hyphens that fit", "[idfxx][font]") {
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB\xC2\xAD" "AB", 9, {{"AB", 8, true}, {"AB", 6, false}}));
    TEST_ASSERT_TRUE(
        breaks_into(prop_font, "B AA\xC2\xAD" "A", 11, {{"B", 2, false}, {"AA", 10, true}, {"A", 4, false}})
    );
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB\xC2\xAD" "AB", 7, {{"AB", 6, false}, {"AB", 6, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "A\xC2\xAD" "B", 6, {{"A\xC2\xAD" "B", 6, false}}));
}

TEST_CASE("font break_lines splits words too long for a line", "[idfxx][font]") {
    TEST_ASSERT_TRUE(breaks_into(prop_font, "B AAAA", 10, {{"B", 2, false}, {"AA", 8, false}, {"AA", 8, false}}));
    TEST_ASSERT_TRUE(breaks_into(prop_font, "AB A", 0, {{"A", 4, false}, {"B", 2, false}, {"A", 4, false}}));
    // Splits fall between UTF-8 code points.
    TEST_ASSERT_TRUE(breaks_into(
        prop_font,
        "\xE2\x80\x99\xE2\x80\x99\xE2\x80\x99",
        7,
        {{"\xE2\x80\x99\xE2\x80\x99", 6, false}, {"\xE2\x80\x99", 3, false}}
    ));
}

TEST_CASE("font break_lines measures mono text in scaled cells", "[idfxx][font]") {
    TEST_ASSERT_TRUE(breaks_into(test_font, "AB AB", 20, {{"AB", 20, false}, {"AB", 20, false}}, 2));
    TEST_ASSERT_TRUE(breaks_into(test_font, "AB-AB", 15, {{"AB-", 15, false}, {"AB", 10, false}}));
    TEST_ASSERT_TRUE(breaks_into(test_font, "AB AB", 25, {{"AB AB", 25, false}}, 0));
}

TEST_CASE("font text_width ignores soft hyphens", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(5, prop_font.advance(U'\u00AD'));
    TEST_ASSERT_EQUAL(6, text_width(prop_font, "A\xC2\xAD" "B"));
    TEST_ASSERT_EQUAL(6, text_width(prop_font_replaced_soft_hyphen, "A\xC2\xAD" "B"));
}
