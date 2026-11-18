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
#include <vector>

using namespace idfxx::font;

namespace {

// The bundled fonts of each weight, smallest first.
const std::array<const proportional_font*, 4> regular{&inter_16, &inter_24, &inter_32, &inter_48};
const std::array<const proportional_font*, 4> medium{
    &inter_medium_16, &inter_medium_24, &inter_medium_32, &inter_medium_48
};
const std::array<const std::array<const proportional_font*, 4>*, 2> weights{&regular, &medium};

// Every bundled font.
const std::array<const proportional_font*, 8> fonts{
    &inter_16, &inter_24, &inter_32, &inter_48,
    &inter_medium_16, &inter_medium_24, &inter_medium_32, &inter_medium_48
};

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

// Returns the widths of the runs of ink in one row of g, left to right.
std::vector<size_t> ink_runs(const proportional_font& font, const glyph_metrics& g, size_t row) {
    std::vector<size_t> runs;
    size_t run = 0;
    for (size_t col = 0; col < g.width; ++col) {
        if (font.ink_at(g, col, row)) {
            ++run;
        } else if (run > 0) {
            runs.push_back(run);
            run = 0;
        }
    }
    if (run > 0) {
        runs.push_back(run);
    }
    return runs;
}

} // namespace

TEST_CASE("inter fonts have the documented line heights", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(20, inter_16.line_height);
    TEST_ASSERT_EQUAL(30, inter_24.line_height);
    TEST_ASSERT_EQUAL(39, inter_32.line_height);
    TEST_ASSERT_EQUAL(59, inter_48.line_height);
    TEST_ASSERT_EQUAL(20, inter_medium_16.line_height);
    TEST_ASSERT_EQUAL(30, inter_medium_24.line_height);
    TEST_ASSERT_EQUAL(39, inter_medium_32.line_height);
    TEST_ASSERT_EQUAL(59, inter_medium_48.line_height);
}

TEST_CASE("inter fonts have sensible line metrics", "[idfxx][font]") {
    for (const proportional_font* font : fonts) {
        TEST_ASSERT_GREATER_THAN(0, font->ascent);
        TEST_ASSERT_GREATER_THAN(0, font->descent);
        TEST_ASSERT_GREATER_THAN(font->descent, font->ascent);
        TEST_ASSERT_GREATER_OR_EQUAL(font->ascent + font->descent, font->line_height);
    }
    for (const auto* sizes : weights) {
        for (size_t i = 1; i < sizes->size(); ++i) {
            const proportional_font& smaller = *(*sizes)[i - 1];
            const proportional_font& larger = *(*sizes)[i];
            TEST_ASSERT_GREATER_THAN(smaller.ascent, larger.ascent);
            TEST_ASSERT_GREATER_THAN(smaller.descent, larger.descent);
            TEST_ASSERT_GREATER_THAN(smaller.line_height, larger.line_height);
        }
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
    for (const auto* sizes : weights) {
        for (size_t i = 1; i < sizes->size(); ++i) {
            const size_t smaller = text_width(*(*sizes)[i - 1], "Hello");
            TEST_ASSERT_GREATER_THAN(smaller, text_width(*(*sizes)[i], "Hello"));
        }
    }
}

TEST_CASE("inter fonts give each glyph's stems one width", "[idfxx][font]") {
    struct stemmed {
        char32_t cp;
        size_t stems;
    };
    constexpr std::array<stemmed, 5> glyphs{
        {{U'H', 2}, {U'h', 2}, {U'n', 2}, {U'u', 2}, {U'm', 3}}
    };
    for (const proportional_font* font : fonts) {
        const int x_height = font->find(U'x')->height; // flat top and bottom
        for (const stemmed& s : glyphs) {
            const glyph_metrics& g = *font->find(s.cp);
            size_t checked = 0;
            for (size_t row = 0; row < g.height; ++row) {
                // Only the straight part of the stems: rows through an arch (the shoulders of h, n
                // and m, the bowl of u) also show one run per stem, of differing widths.
                const int y = g.y_offset + static_cast<int>(row); // negative above the baseline
                if (s.cp == U'u' && y >= -x_height + x_height / 2) {
                    continue; // lower half of the x-height: the bowl
                }
                if ((s.cp == U'h' || s.cp == U'n' || s.cp == U'm') && y < -(x_height / 2)) {
                    continue; // the shoulders, and the ascender of h
                }
                const std::vector<size_t> runs = ink_runs(*font, g, row);
                if (runs.size() != s.stems) {
                    continue;
                }
                ++checked;
                for (size_t run : runs) {
                    TEST_ASSERT_EQUAL_MESSAGE(runs.front(), run, "stems of unequal width");
                }
            }
            TEST_ASSERT_GREATER_THAN_MESSAGE(0, checked, "no row crosses every stem");
        }
    }
}
