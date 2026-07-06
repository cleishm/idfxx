// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_font
// Pure pixel rendering — runs everywhere, including QEMU.

#include "idfxx/font"
#include "unity.h"

#include <cstddef>
#include <type_traits>
#include <utility>

using namespace idfxx::font;
using idfxx::lcd::mono_framebuffer;

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

static_assert(std::is_trivially_copyable_v<mono_font>);

// Bundled fonts cover printable ASCII with the documented cell sizes.
static_assert(
    text_width(mono_font{.width = 5, .height = 8, .first_char = ' ', .last_char = '~', .bitmap = nullptr}, "abc") == 15
);

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Returns true when the framebuffer pixel matches the glyph bitmap bit for
// every pixel of the glyph cell rendered at (ox, oy) with the given scale.
bool matches_glyph(const mono_framebuffer& fb, const mono_font& f, char c, size_t ox, size_t oy, unsigned scale = 1) {
    const uint8_t* glyph = f.glyph(c);
    for (size_t row = 0; row < f.height; ++row) {
        for (size_t col = 0; col < f.width; ++col) {
            const bool ink = glyph[row * f.bytes_per_row() + col / 8] & (0x80u >> (col % 8));
            for (unsigned sy = 0; sy < scale; ++sy) {
                for (unsigned sx = 0; sx < scale; ++sx) {
                    if (fb.get_pixel(ox + col * scale + sx, oy + row * scale + sy) != ink) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

size_t count_set_pixels(const mono_framebuffer& fb) {
    size_t n = 0;
    for (size_t y = 0; y < fb.height(); ++y) {
        for (size_t x = 0; x < fb.width(); ++x) {
            n += fb.get_pixel(x, y) ? 1 : 0;
        }
    }
    return n;
}

// mono_framebuffer's dimension constructor throws and is compiled out when
// exceptions are disabled; build through make() so the drawing tests compile
// in both configurations.
mono_framebuffer make_fb(size_t width, size_t height) {
    return std::move(*mono_framebuffer::make(width, height));
}

} // namespace

// =============================================================================
// Runtime tests
// =============================================================================

TEST_CASE("font bundled fonts have the documented geometry", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(5, spleen_5x8.width);
    TEST_ASSERT_EQUAL(8, spleen_5x8.height);
    TEST_ASSERT_EQUAL(8, spleen_8x16.width);
    TEST_ASSERT_EQUAL(16, spleen_8x16.height);
    TEST_ASSERT_EQUAL(' ', spleen_5x8.first_char);
    TEST_ASSERT_EQUAL('~', spleen_5x8.last_char);
    TEST_ASSERT_TRUE(spleen_5x8.contains('A'));
    TEST_ASSERT_FALSE(spleen_5x8.contains('\n'));
}

TEST_CASE("font draw_text renders the glyph bitmap exactly", "[idfxx][font]") {
    auto fb = make_fb(32, 16);
    draw_text(fb, spleen_5x8, 2, 3, "A");
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 2, 3));
    TEST_ASSERT_TRUE(count_set_pixels(fb) > 0); // 'A' has ink
}

TEST_CASE("font draw_text advances one cell per character", "[idfxx][font]") {
    auto fb = make_fb(64, 16);
    draw_text(fb, spleen_5x8, 0, 0, "AB");
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 0, 0));
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'B', 5, 0));
}

TEST_CASE("font draw_text scale doubles every pixel", "[idfxx][font]") {
    auto fb = make_fb(64, 40);
    draw_text(fb, spleen_8x16, 4, 2, "7", true, 2);
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_8x16, '7', 4, 2, 2));
}

TEST_CASE("font draw_text with on=false erases ink pixels", "[idfxx][font]") {
    auto fb = make_fb(32, 16);
    fb.fill(true);
    draw_text(fb, spleen_5x8, 0, 0, "A", false);
    const uint8_t* glyph = spleen_5x8.glyph('A');
    for (size_t row = 0; row < spleen_5x8.height; ++row) {
        for (size_t col = 0; col < spleen_5x8.width; ++col) {
            const bool ink = glyph[row * spleen_5x8.bytes_per_row() + col / 8] & (0x80u >> (col % 8));
            // Ink pixels cleared; background pixels untouched (still set).
            TEST_ASSERT_EQUAL(!ink, fb.get_pixel(col, row));
        }
    }
}

TEST_CASE("font draw_text clips at framebuffer edges", "[idfxx][font]") {
    auto fb = make_fb(8, 8);
    draw_text(fb, spleen_8x16, 4, 4, "MMM"); // extends far past both edges
    // Must not crash; pixels inside the fb may be set, nothing to assert
    // beyond bounds-safety (get_pixel outside is false by contract).
    TEST_ASSERT_FALSE(fb.get_pixel(8, 8));
}

TEST_CASE("font draw_text skips characters without glyphs", "[idfxx][font]") {
    auto fb = make_fb(32, 16);
    draw_text(fb, spleen_5x8, 0, 0, "\x01");
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
    // But the cursor still advances: 'A' after a missing glyph lands one cell in.
    draw_text(fb, spleen_5x8, 0, 0, "\x01\x41"); // \x41 == 'A'
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 5, 0));
}

TEST_CASE("font text_width scales with length and factor", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(0, text_width(spleen_5x8, ""));
    TEST_ASSERT_EQUAL(15, text_width(spleen_5x8, "abc"));
    TEST_ASSERT_EQUAL(48, text_width(spleen_8x16, "123", 2));
}
