// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_font_inter
// Pure font-data checks — runs everywhere, including QEMU.

#include "idfxx/font/inter"
#include "unity.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>

using namespace idfxx::font;

namespace {

// The bundled fonts, smallest first.
const std::array<const proportional_font*, 4> fonts{&inter_16, &inter_24, &inter_32, &inter_48};

// Returns true if any pixel of g is inked.
bool has_ink(const proportional_font& font, const glyph_metrics& g) {
    for (size_t row = 0; row < g.height; ++row) {
        for (size_t col = 0; col < g.width; ++col) {
            if (font.ink_at(g, col, row)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("inter fonts have the documented line heights", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(20, inter_16.line_height);
    TEST_ASSERT_EQUAL(30, inter_24.line_height);
    TEST_ASSERT_EQUAL(39, inter_32.line_height);
    TEST_ASSERT_EQUAL(59, inter_48.line_height);
}

TEST_CASE("inter fonts have sensible line metrics", "[idfxx][font]") {
    for (const proportional_font* font : fonts) {
        TEST_ASSERT_GREATER_THAN(0, font->ascent);
        TEST_ASSERT_GREATER_THAN(0, font->descent);
        TEST_ASSERT_GREATER_THAN(font->descent, font->ascent);
        TEST_ASSERT_GREATER_OR_EQUAL(font->ascent + font->descent, font->line_height);
    }
    for (size_t i = 1; i < fonts.size(); ++i) {
        TEST_ASSERT_GREATER_THAN(fonts[i - 1]->ascent, fonts[i]->ascent);
        TEST_ASSERT_GREATER_THAN(fonts[i - 1]->descent, fonts[i]->descent);
        TEST_ASSERT_GREATER_THAN(fonts[i - 1]->line_height, fonts[i]->line_height);
    }
}

TEST_CASE("inter fonts cover printable ASCII with ink", "[idfxx][font]") {
    for (const proportional_font* font : fonts) {
        const glyph_metrics* space = font->find(U' ');
        TEST_ASSERT_NOT_NULL(space);
        TEST_ASSERT_FALSE(has_ink(*font, *space));
        TEST_ASSERT_GREATER_THAN(0, space->advance);
        for (char32_t cp = U'!'; cp <= U'~'; ++cp) {
            const glyph_metrics* g = font->find(cp);
            TEST_ASSERT_NOT_NULL_MESSAGE(g, "printable ASCII glyph missing");
            TEST_ASSERT_TRUE_MESSAGE(has_ink(*font, *g), "printable glyph unexpectedly blank");
            TEST_ASSERT_GREATER_THAN(0, g->advance);
        }
    }
}

TEST_CASE("inter fonts cover accents and typographic punctuation", "[idfxx][font]") {
    // é, ’, “, ”, €, —
    constexpr std::array<char32_t, 6> extras{
        U'\u00E9', U'\u2019', U'\u201C', U'\u201D', U'\u20AC', U'\u2014'
    };
    for (const proportional_font* font : fonts) {
        for (char32_t cp : extras) {
            const glyph_metrics* g = font->find(cp);
            TEST_ASSERT_NOT_NULL_MESSAGE(g, "Windows-1252 glyph missing");
            TEST_ASSERT_TRUE(has_ink(*font, *g));
        }
        // The soft hyphen has no glyph; text measurement ignores it.
        TEST_ASSERT_FALSE(font->contains(U'\u00AD'));
        TEST_ASSERT_EQUAL(text_width(*font, "inter"), text_width(*font, "in\xC2\xAD" "ter"));
    }
}

TEST_CASE("inter fonts draw uncovered code points as '?'", "[idfxx][font]") {
    for (const proportional_font* font : fonts) {
        TEST_ASSERT_NOT_NULL(font->replacement);
        TEST_ASSERT_EQUAL_PTR(font->find(U'?'), font->replacement);
        TEST_ASSERT_FALSE(font->contains(U'\u4E2D'));
        TEST_ASSERT_EQUAL_PTR(font->replacement, font->resolve(U'\u4E2D'));
        TEST_ASSERT_EQUAL(font->advance(U'?'), font->advance(U'\U0001F600'));
    }
}

TEST_CASE("inter fonts' ink extents bound every glyph exactly", "[idfxx][font]") {
    for (const proportional_font* font : fonts) {
        int ascent = 0;
        int descent = 0;
        int left = INT_MAX;
        int right = INT_MAX;
        for (const glyph_metrics& g : font->glyphs) {
            if (g.width == 0 || g.height == 0) {
                continue; // no ink
            }
            const int top = -g.y_offset;
            const int bottom = g.y_offset + g.height;
            const int left_bearing = g.x_offset;
            const int right_bearing = g.advance - (g.x_offset + g.width);
            TEST_ASSERT_LESS_OR_EQUAL(font->ink_ascent, top);
            TEST_ASSERT_LESS_OR_EQUAL(font->ink_descent, bottom);
            TEST_ASSERT_GREATER_OR_EQUAL(font->min_left_bearing, left_bearing);
            TEST_ASSERT_GREATER_OR_EQUAL(font->min_right_bearing, right_bearing);
            ascent = std::max(ascent, top);
            descent = std::max(descent, bottom);
            left = std::min(left, left_bearing);
            right = std::min(right, right_bearing);
        }
        // The replacement is drawn for every uncovered code point; it must be bounded too.
        const glyph_metrics& r = *font->replacement;
        TEST_ASSERT_LESS_OR_EQUAL(font->ink_ascent, -r.y_offset);
        TEST_ASSERT_LESS_OR_EQUAL(font->ink_descent, r.y_offset + r.height);
        TEST_ASSERT_GREATER_OR_EQUAL(font->min_left_bearing, r.x_offset);
        TEST_ASSERT_GREATER_OR_EQUAL(font->min_right_bearing, r.advance - (r.x_offset + r.width));
        // And the bounds are tight.
        TEST_ASSERT_EQUAL(ascent, font->ink_ascent);
        TEST_ASSERT_EQUAL(descent, font->ink_descent);
        TEST_ASSERT_EQUAL(left, font->min_left_bearing);
        TEST_ASSERT_EQUAL(right, font->min_right_bearing);
    }
}

TEST_CASE("inter text_width grows with size", "[idfxx][font]") {
    for (const proportional_font* font : fonts) {
        TEST_ASSERT_GREATER_THAN(0, text_width(*font, "Hello"));
        // UTF-8 is decoded: "café" measures its four glyphs.
        const size_t cafe = font->advance(U'c') + font->advance(U'a') + font->advance(U'f') +
            font->advance(U'\u00E9');
        TEST_ASSERT_EQUAL(cafe, text_width(*font, "caf\xC3\xA9"));
    }
    for (size_t i = 1; i < fonts.size(); ++i) {
        const size_t smaller = text_width(*fonts[i - 1], "Hello");
        TEST_ASSERT_GREATER_THAN(smaller, text_width(*fonts[i], "Hello"));
    }
}
