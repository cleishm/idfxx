// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_font_inter
// Pure font-data checks — runs everywhere, including QEMU.

#include "idfxx/font/inter"
#include "unity.h"

#include <array>
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
        // Soft hyphen is present but invisible and zero-width.
        const glyph_metrics* shy = font->find(U'\u00AD');
        TEST_ASSERT_NOT_NULL(shy);
        TEST_ASSERT_EQUAL(0, shy->advance);
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
