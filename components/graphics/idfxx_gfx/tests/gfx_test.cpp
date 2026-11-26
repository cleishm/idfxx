// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_gfx
// Pure pixel rendering. Runs everywhere, including QEMU.

#include "idfxx/epaper/color6_framebuffer"
#include "idfxx/epaper/gray4_framebuffer"
#include "idfxx/epaper/mono_framebuffer"
#include "idfxx/epaper/panel"
#include "idfxx/font/spleen"
#include "idfxx/gfx"
#include "idfxx/lcd/mono_framebuffer"
#include "idfxx/lcd/panel"
#include "idfxx/lcd/rgb565_framebuffer"
#include "unity.h"

#include <algorithm>
#include <array>
#include <climits>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

using namespace idfxx::gfx;
using idfxx::font::mono_font;
using idfxx::font::spleen_5x8;
using idfxx::font::spleen_8x16;
using idfxx::lcd::mono_framebuffer;
using idfxx::lcd::rgb565;
using idfxx::lcd::rgb565_framebuffer;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time. If this file compiles, they pass.
// =============================================================================

// The idfxx_lcd framebuffers satisfy pixel_surface.
static_assert(pixel_surface<mono_framebuffer>);
static_assert(pixel_surface<rgb565_framebuffer>);

// Arbitrary types do not.
static_assert(!pixel_surface<int>);
static_assert(!pixel_surface<mono_font>);

namespace {

// A minimal user-defined surface, proving the concept is structural.
struct counting_surface {
    using pixel_type = int;

    std::array<std::array<int, 8>, 8> pixels{};
    size_t writes = 0;

    void set_pixel(size_t x, size_t y, int value) noexcept {
        ++writes;
        if (x < 8 && y < 8) {
            pixels[y][x] = value;
        }
    }
    [[nodiscard]] size_t width() const noexcept { return 8; }
    [[nodiscard]] size_t height() const noexcept { return 8; }
};

} // namespace

static_assert(pixel_surface<counting_surface>);

// A canvas is itself a pixel surface, whatever it wraps.
static_assert(pixel_surface<canvas<mono_framebuffer>>);
static_assert(pixel_surface<canvas<rgb565_framebuffer>>);
static_assert(pixel_surface<canvas<counting_surface>>);

namespace {

// A draw callback accepting a canvas over any band.
struct any_draw {
    template<typename Canvas>
    void operator()(Canvas&) const {}
};

// A destination with no try_write at all.
struct no_write_target {};

} // namespace

// render_banded accepts any destination that can write the band at an
// offset: the LCD panels for their framebuffers, and the ePaper panels for
// theirs.
static_assert(banded_renderable<rgb565_framebuffer, idfxx::lcd::panel, any_draw>);
static_assert(banded_renderable<mono_framebuffer, idfxx::lcd::panel, any_draw>);
static_assert(banded_renderable<idfxx::epaper::mono_framebuffer, idfxx::epaper::panel, any_draw>);
static_assert(banded_renderable<idfxx::epaper::gray4_framebuffer, idfxx::epaper::panel, any_draw>);
static_assert(banded_renderable<idfxx::epaper::color6_framebuffer, idfxx::epaper::panel, any_draw>);

// A destination without a try_write for the band type is rejected.
static_assert(!banded_renderable<mono_framebuffer, no_write_target, any_draw>);
static_assert(!banded_renderable<idfxx::epaper::mono_framebuffer, idfxx::lcd::panel, any_draw>);
static_assert(!banded_renderable<rgb565_framebuffer, idfxx::epaper::panel, any_draw>);
static_assert(!banded_renderable<counting_surface, idfxx::lcd::panel, any_draw>);

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Returns true when the framebuffer pixel matches the glyph bitmap bit for
// every pixel of the glyph cell rendered at (ox, oy) with the given scale.
bool matches_glyph(
    const mono_framebuffer& fb, const mono_font& f, char c, size_t ox, size_t oy, unsigned scale = 1
) {
    for (size_t row = 0; row < f.height; ++row) {
        for (size_t col = 0; col < f.width; ++col) {
            const bool ink = f.ink_at(c, col, row);
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

// Returns true when both framebuffers hold the same pixels.
bool same_pixels(const mono_framebuffer& a, const mono_framebuffer& b) {
    for (size_t y = 0; y < a.height(); ++y) {
        for (size_t x = 0; x < a.width(); ++x) {
            if (a.get_pixel(x, y) != b.get_pixel(x, y)) {
                return false;
            }
        }
    }
    return true;
}

// Returns true when band holds the rows of full starting at row y0.
bool band_matches(const mono_framebuffer& full, const mono_framebuffer& band, size_t y0) {
    for (size_t y = 0; y < band.height(); ++y) {
        for (size_t x = 0; x < band.width(); ++x) {
            if (full.get_pixel(x, y0 + y) != band.get_pixel(x, y)) {
                return false;
            }
        }
    }
    return true;
}

// mono_framebuffer's dimension constructor throws and is compiled out when
// exceptions are disabled. Build through make() so that the drawing tests
// compile in both configurations.
mono_framebuffer make_fb(size_t width, size_t height) {
    return std::move(*mono_framebuffer::make(width, height));
}

rgb565_framebuffer make_color_fb(size_t width, size_t height) {
    return std::move(*rgb565_framebuffer::make(width, height));
}

// A bool surface of configurable size that counts every set_pixel call,
// including ones outside its bounds, to prove clipped work is never attempted.
template<size_t W, size_t H>
struct sized_counter {
    using pixel_type = bool;

    std::array<bool, W * H> pixels{};
    size_t writes = 0;

    void set_pixel(size_t x, size_t y, bool value) noexcept {
        ++writes;
        if (x < W && y < H) {
            pixels[y * W + x] = value;
        }
    }
    [[nodiscard]] size_t width() const noexcept { return W; }
    [[nodiscard]] size_t height() const noexcept { return H; }
    [[nodiscard]] bool get_pixel(size_t x, size_t y) const noexcept { return pixels[y * W + x]; }
};

// Counts the set pixels of rows [y0, y1) of the framebuffer.
size_t count_set_rows(const mono_framebuffer& fb, size_t y0, size_t y1) {
    size_t n = 0;
    for (size_t y = y0; y < y1 && y < fb.height(); ++y) {
        for (size_t x = 0; x < fb.width(); ++x) {
            n += fb.get_pixel(x, y) ? 1 : 0;
        }
    }
    return n;
}

// Renders draw(canvas) onto an unclipped (32 + dx)x32 frame and again into
// four 32x8 bands through canvas(band, dx, y, w, h), each covering frame columns
// [dx, dx + 32). Returns true when every band equals its part of the frame
// and the frame has ink.
template<typename Draw>
bool bands_match_frame(Draw draw, size_t dx = 0) {
    auto full = make_fb(32 + dx, 32);
    canvas full_canvas(full);
    draw(full_canvas);
    if (count_set_pixels(full) == 0) {
        return false;
    }
    for (size_t y0 = 0; y0 < 32; y0 += 8) {
        auto band = make_fb(32, 8);
        canvas band_canvas(band, dx, y0, 32 + dx, 32);
        draw(band_canvas);
        for (size_t y = 0; y < 8; ++y) {
            for (size_t x = 0; x < 32; ++x) {
                if (band.get_pixel(x, y) != full.get_pixel(x + dx, y0 + y)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

// =============================================================================
// Runtime tests: primitives
// =============================================================================

TEST_CASE("gfx fill_rect fills exactly the requested pixels", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    fill_rect(fb, 2, 3, 4, 5, true);
    for (size_t y = 0; y < fb.height(); ++y) {
        for (size_t x = 0; x < fb.width(); ++x) {
            const bool inside = x >= 2 && x < 6 && y >= 3 && y < 8;
            TEST_ASSERT_EQUAL(inside, fb.get_pixel(x, y));
        }
    }
}

TEST_CASE("gfx fill_rect clips at the surface edges", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);

    // Overhanging both edges clips to the surface.
    fill_rect(fb, 12, 12, 100, 100, true);
    TEST_ASSERT_EQUAL(4 * 4, count_set_pixels(fb));

    // Fully outside draws nothing (and terminates).
    fb.clear();
    fill_rect(fb, 16, 0, SIZE_MAX, SIZE_MAX, true);
    fill_rect(fb, 0, 16, 1, 1, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

TEST_CASE("gfx draw_rect outlines without filling", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    draw_rect(fb, 2, 3, 5, 4, true);
    // Border pixels set: 2*5 + 2*(4-2) = 14.
    TEST_ASSERT_EQUAL(14, count_set_pixels(fb));
    // Corners and edges.
    TEST_ASSERT_TRUE(fb.get_pixel(2, 3));
    TEST_ASSERT_TRUE(fb.get_pixel(6, 3));
    TEST_ASSERT_TRUE(fb.get_pixel(2, 6));
    TEST_ASSERT_TRUE(fb.get_pixel(6, 6));
    // Interior untouched.
    TEST_ASSERT_FALSE(fb.get_pixel(4, 5));
}

TEST_CASE("gfx hline and vline draw single-pixel runs", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    draw_hline(fb, 1, 2, 5, true);
    draw_vline(fb, 8, 4, 3, true);
    TEST_ASSERT_EQUAL(8, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(1, 2));
    TEST_ASSERT_TRUE(fb.get_pixel(5, 2));
    TEST_ASSERT_FALSE(fb.get_pixel(6, 2));
    TEST_ASSERT_TRUE(fb.get_pixel(8, 4));
    TEST_ASSERT_TRUE(fb.get_pixel(8, 6));
    TEST_ASSERT_FALSE(fb.get_pixel(8, 7));
}

TEST_CASE("gfx draw_line connects endpoints in any direction", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);

    // Diagonal down-right: both endpoints inclusive.
    draw_line(fb, 0, 0, 5, 5, true);
    for (size_t i = 0; i <= 5; ++i) {
        TEST_ASSERT_TRUE(fb.get_pixel(i, i));
    }
    TEST_ASSERT_EQUAL(6, count_set_pixels(fb));

    // Up-left (reversed endpoints) draws the same pixels.
    fb.clear();
    draw_line(fb, 5, 5, 0, 0, true);
    TEST_ASSERT_EQUAL(6, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(5, 5));

    // A single point.
    fb.clear();
    draw_line(fb, 7, 7, 7, 7, true);
    TEST_ASSERT_EQUAL(1, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(7, 7));

    // Shallow slope hits both endpoints.
    fb.clear();
    draw_line(fb, 0, 0, 9, 3, true);
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(9, 3));
    TEST_ASSERT_EQUAL(10, count_set_pixels(fb)); // one pixel per column
}

TEST_CASE("gfx primitives work on user-defined surfaces", "[idfxx][gfx]") {
    counting_surface surface;
    fill_rect(surface, 1, 1, 2, 2, 7);
    TEST_ASSERT_EQUAL(4, surface.writes);
    TEST_ASSERT_EQUAL(7, surface.pixels[1][1]);
    TEST_ASSERT_EQUAL(7, surface.pixels[2][2]);
    TEST_ASSERT_EQUAL(0, surface.pixels[0][0]);
}

// =============================================================================
// Runtime tests: canvas
// =============================================================================

TEST_CASE("gfx canvas members draw like the free functions", "[idfxx][gfx]") {
    auto fb = make_fb(32, 16);
    canvas c(fb);

    TEST_ASSERT_EQUAL(fb.width(), c.width());
    TEST_ASSERT_EQUAL(fb.height(), c.height());
    TEST_ASSERT_EQUAL_PTR(&fb, &c.surface());

    c.set_pixel(1, 1, true);
    TEST_ASSERT_TRUE(fb.get_pixel(1, 1));

    c.fill_rect(2, 3, 4, 5, true);
    c.draw_rect(8, 3, 5, 4, true);
    c.draw_hline(0, 14, 5, true);
    c.draw_vline(20, 0, 3, true);
    c.draw_line(24, 0, 29, 5, true);
    c.draw_text(spleen_5x8, 14, 8, "A");

    auto expected = make_fb(32, 16);
    expected.set_pixel(1, 1, true);
    fill_rect(expected, 2, 3, 4, 5, true);
    draw_rect(expected, 8, 3, 5, 4, true);
    draw_hline(expected, 0, 14, 5, true);
    draw_vline(expected, 20, 0, 3, true);
    draw_line(expected, 24, 0, 29, 5, true);
    draw_text(expected, spleen_5x8, 14, 8, "A");

    for (size_t y = 0; y < fb.height(); ++y) {
        for (size_t x = 0; x < fb.width(); ++x) {
            TEST_ASSERT_EQUAL(expected.get_pixel(x, y), fb.get_pixel(x, y));
        }
    }
}

TEST_CASE("gfx canvas fill and clear reach every pixel", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    canvas c(fb);

    c.fill(true);
    TEST_ASSERT_EQUAL(16 * 16, count_set_pixels(fb));

    c.clear();
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

TEST_CASE("gfx canvas fill and clear fall back to per-pixel writes", "[idfxx][gfx]") {
    // counting_surface has no fill()/clear() members, so the canvas writes
    // each pixel through set_pixel.
    counting_surface surface;
    canvas c(surface);

    c.fill(9);
    TEST_ASSERT_EQUAL(8 * 8, surface.writes);
    TEST_ASSERT_EQUAL(9, surface.pixels[0][0]);
    TEST_ASSERT_EQUAL(9, surface.pixels[7][7]);

    c.clear();
    TEST_ASSERT_EQUAL(2 * 8 * 8, surface.writes);
    TEST_ASSERT_EQUAL(0, surface.pixels[3][4]);
}

TEST_CASE("gfx canvas satisfies pixel_surface for the free functions", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    canvas c(fb);
    fill_rect(c, 2, 2, 3, 3, true);
    TEST_ASSERT_EQUAL(9, count_set_pixels(fb));
}

TEST_CASE("gfx canvas draws color ink on an rgb565 surface", "[idfxx][gfx]") {
    auto fb = make_color_fb(16, 8);
    canvas c(fb);
    constexpr rgb565 red(255, 0, 0);

    c.fill_rect(1, 1, 2, 2, red);
    TEST_ASSERT_TRUE(fb.get_pixel(1, 1) == red);
    TEST_ASSERT_TRUE(fb.get_pixel(2, 2) == red);
    TEST_ASSERT_TRUE(fb.get_pixel(3, 3) == rgb565{});

    c.clear();
    TEST_ASSERT_TRUE(fb.get_pixel(1, 1) == rgb565{});
}

// =============================================================================
// Runtime tests: translated canvas and window
// =============================================================================

namespace {

// Draws a scene spanning a full 32x32 frame, with content deliberately
// crossing the y=16 band boundary.
template<typename Canvas>
void draw_band_test_scene(Canvas& c) {
    c.draw_text(spleen_8x16, 2, 8, "AB"); // glyph rows straddle y=16
    c.fill_rect(20, 10, 8, 12, true);     // rectangle straddles y=16
    c.draw_line(0, 31, 31, 0, true);      // diagonal crosses both bands
    c.draw_rect(1, 1, 30, 30, true);      // frame with edges in both bands
}

} // namespace

TEST_CASE("gfx translated canvas maps canvas coordinates onto the surface", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    canvas c(fb, 4, 16, 20, 24); // fb's top-left sits at (4, 16)

    TEST_ASSERT_EQUAL(4 + 16, c.width());
    TEST_ASSERT_EQUAL(16 + 8, c.height());

    c.set_pixel(4, 16, true);  // surface (0, 0)
    c.set_pixel(19, 23, true); // surface (15, 7)
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(15, 7));
    TEST_ASSERT_EQUAL(2, count_set_pixels(fb));

    // Left of / above the surface: dropped.
    c.set_pixel(3, 16, true);
    c.set_pixel(4, 15, true);
    c.set_pixel(0, 0, true);
    TEST_ASSERT_EQUAL(2, count_set_pixels(fb));

    // At or beyond width()/height(): dropped.
    c.set_pixel(20, 16, true);
    c.set_pixel(4, 24, true);
    TEST_ASSERT_EQUAL(2, count_set_pixels(fb));
}

TEST_CASE("gfx translated canvas renders bands identical to a full frame", "[idfxx][gfx]") {
    auto reference = make_fb(32, 32);
    canvas ref_canvas(reference);
    draw_band_test_scene(ref_canvas);

    auto band = make_fb(32, 16);
    for (size_t band_y = 0; band_y < 32; band_y += 16) {
        canvas c(band, 0, band_y, 32, 32);
        c.clear();
        draw_band_test_scene(c);
        TEST_ASSERT_TRUE(band_matches(reference, band, band_y));
    }
}

TEST_CASE("gfx translated canvas fill and clear cover the whole band", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    canvas c(fb, 0, 24, 16, 32);
    c.fill(true);
    TEST_ASSERT_EQUAL(16 * 8, count_set_pixels(fb));
    c.clear();
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

TEST_CASE("gfx canvas window draws in local coordinates with clipping", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    canvas c(fb);
    auto w = c.window(4, 8, 8, 4);

    TEST_ASSERT_EQUAL(8, w.width());
    TEST_ASSERT_EQUAL(4, w.height());

    w.set_pixel(0, 0, true); // surface (4, 8)
    w.set_pixel(7, 3, true); // surface (11, 11)
    TEST_ASSERT_TRUE(fb.get_pixel(4, 8));
    TEST_ASSERT_TRUE(fb.get_pixel(11, 11));

    // Beyond the window bounds: dropped, even though inside the surface.
    w.set_pixel(8, 0, true);
    w.set_pixel(0, 4, true);
    TEST_ASSERT_EQUAL(2, count_set_pixels(fb));

    // Drawing members clip to the window too.
    w.fill_rect(0, 0, 100, 100, true);
    TEST_ASSERT_EQUAL(8 * 4, count_set_pixels(fb));
}

TEST_CASE("gfx canvas window clear only clears the window", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    fb.fill(true);
    canvas c(fb);
    c.window(4, 8, 8, 4).clear();
    TEST_ASSERT_EQUAL(16 * 16 - 8 * 4, count_set_pixels(fb));
    TEST_ASSERT_FALSE(fb.get_pixel(4, 8));
    TEST_ASSERT_FALSE(fb.get_pixel(11, 11));
    TEST_ASSERT_TRUE(fb.get_pixel(3, 8));
    TEST_ASSERT_TRUE(fb.get_pixel(4, 7));
    TEST_ASSERT_TRUE(fb.get_pixel(12, 11));
}

TEST_CASE("gfx canvas window composes and clamps", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    canvas c(fb);

    // Nested windows compose their offsets and bounds.
    auto inner = c.window(2, 2, 12, 12).window(3, 4, 4, 4);
    TEST_ASSERT_EQUAL(4, inner.width());
    TEST_ASSERT_EQUAL(4, inner.height());
    inner.set_pixel(0, 0, true); // surface (5, 6)
    TEST_ASSERT_TRUE(fb.get_pixel(5, 6));

    // A window extending past the parent's bounds clamps.
    auto clipped = c.window(12, 12, 100, 100);
    TEST_ASSERT_EQUAL(4, clipped.width());
    TEST_ASSERT_EQUAL(4, clipped.height());

    // A window entirely outside the parent is empty and draws nothing.
    auto empty = c.window(16, 0, 4, 4);
    TEST_ASSERT_EQUAL(0, empty.width());
    empty.fill(true);
    TEST_ASSERT_EQUAL(1, count_set_pixels(fb)); // only the pixel set above
}

TEST_CASE("gfx clipping: nothing above a band reaches the surface", "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf, 0, 16, 8, 24); // the surface backs rows 16-23 only

    c.draw_text(spleen_5x8, 0, 0, "ABC");
    c.draw_text(spleen_5x8, 0, 0, "ABC", true, 2); // rows 0-15
    c.draw_text(spleen_8x16, 0, 0, "A");
    c.fill_rect(0, 0, 8, 16, true);
    c.draw_rect(0, 0, 8, 16, true);
    c.draw_line(0, 0, 7, 15, true);
    c.draw_line(7, 15, 0, 0, true);
    TEST_ASSERT_EQUAL(0, surf.writes);

    // Sanity: content that reaches the band does write.
    c.fill_rect(0, 16, 8, 1, true);
    TEST_ASSERT_EQUAL(8, surf.writes);
}

TEST_CASE("gfx clipping: free functions on a canvas clip to its band", "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf, 0, 16, 8, 24);

    draw_text(c, spleen_5x8, 0, 0, "ABC", true, 1);
    draw_text(c, spleen_5x8, 0, 0, "ABC", true, 2);
    fill_rect(c, 0, 0, 8, 16, true);
    draw_rect(c, 0, 0, 8, 16, true);
    draw_line(c, 0, 0, 7, 15, true);
    TEST_ASSERT_EQUAL(0, surf.writes);
}

TEST_CASE("gfx clipping: a window outside the band draws nothing", "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf, 0, 16, 8, 24);
    auto w = c.window(0, 0, 8, 8); // canvas rows 0-7, above the band

    w.draw_text(spleen_5x8, 0, 0, "AB");
    w.draw_text(spleen_5x8, 0, 0, "AB", true, 2);
    w.fill_rect(0, 0, 8, 8, true);
    w.draw_rect(0, 0, 8, 8, true);
    w.draw_line(0, 0, 7, 7, true);
    w.draw_text_box(spleen_5x8, 0, 0, 8, "AB CD");
    draw_text(w, spleen_5x8, 0, 0, "A", true, 1);
    fill_rect(w, 0, 0, 8, 8, true);
    TEST_ASSERT_EQUAL(0, surf.writes);
}

TEST_CASE("gfx clipping: mono text straddling a band edge writes only visible ink",
          "[idfxx][gfx]") {
    for (unsigned scale = 1; scale <= 3; ++scale) {
        auto full = make_fb(32, 32);
        draw_text(full, spleen_5x8, 0, 0, "ABC", true, scale);
        TEST_ASSERT_TRUE(count_set_pixels(full) > 0);

        // Every band position, so edges fall mid-block at scale 2 and 3.
        for (size_t y0 = 0; y0 <= 24; ++y0) {
            sized_counter<32, 8> surf;
            canvas c(surf, 0, y0, 32, 32);
            c.draw_text(spleen_5x8, 0, 0, "ABC", true, scale);
            TEST_ASSERT_EQUAL(count_set_rows(full, y0, y0 + 8), surf.writes);
        }
    }
}

TEST_CASE("gfx clipping: mono text renders bands identical to a full frame at scale 2 and 3",
          "[idfxx][gfx]") {
    // The band edges (rows 8, 16, 24) fall mid-block in the scaled glyphs.
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_text(spleen_5x8, 1, 3, "AB", true, 2); })
    );
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_text(spleen_5x8, 1, 1, "AB", true, 3); })
    );
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_text(spleen_5x8, 2, 4, "MW", true, 3); })
    );
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_text(spleen_8x16, 0, 2, "Q", true, 2); })
    );
}

TEST_CASE("gfx clipping: lines render bands identical to a full frame", "[idfxx][gfx]") {
    // Steep and shallow diagonals in every direction, crossing several bands.
    // steep, down
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(3, 0, 10, 31, true); })
    );
    // shallow, right
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(0, 3, 31, 10, true); })
    );
    // right to left
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(31, 2, 0, 29, true); })
    );
    // bottom to top
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(28, 31, 5, 1, true); })
    );
    // steep, up
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(20, 31, 24, 0, true); })
    );
    // 45 degrees
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(0, 31, 31, 0, true); })
    );
    // shallow, up-left
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(30, 9, 1, 23, true); })
    );
    // vertical
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(7, 0, 7, 31, true); })
    );
    // horizontal
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(0, 12, 31, 12, true); })
    );
    // single point
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_line(9, 17, 9, 17, true); })
    );
}

TEST_CASE("gfx clipping: a canvas with a nonzero x origin renders bands identical to a full frame",
          "[idfxx][gfx]") {
    TEST_ASSERT_TRUE(
        bands_match_frame([](auto& c) { c.draw_text(spleen_5x8, 3, 5, "AB", true, 2); }, 5)
    );
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { c.draw_line(8, 2, 30, 30, true); }, 5));
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { c.fill_rect(6, 5, 20, 22, true); }, 5));
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { c.draw_rect(5, 3, 20, 25, true); }, 5));
}

TEST_CASE("gfx clipping: a window partly off-band renders bands identical to a full frame",
          "[idfxx][gfx]") {
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) {
        auto w = c.window(2, 6, 20, 12); // spans the y=8 and y=16 band edges
        w.fill_rect(1, 1, 5, 20, true);
        w.draw_text(spleen_5x8, 3, 2, "AB", true, 2);
        w.draw_line(0, 0, 30, 20, true);
        w.draw_rect(0, 0, 20, 12, true);
    }));
    TEST_ASSERT_TRUE(bands_match_frame(
        [](auto& c) {
            auto w = c.window(2, 6, 20, 12);
            w.draw_text(spleen_8x16, 1, 0, "Z", true, 1);
            w.draw_text_box(spleen_5x8, 0, 0, 20, "AB CD E");
        },
        3
    ));
}

// =============================================================================
// Runtime tests: rotated
// =============================================================================

// A rotated view is itself a pixel surface, and a canvas can wrap one.
static_assert(pixel_surface<rotated<mono_framebuffer>>);
static_assert(pixel_surface<canvas<rotated<mono_framebuffer>>>);
static_assert(pixel_surface<rotated<counting_surface>>);

TEST_CASE("gfx rotated view swaps the reported dimensions", "[idfxx][gfx]") {
    auto fb = make_fb(8, 16);
    rotated view(fb, rotation::cw90);
    TEST_ASSERT_EQUAL(16, view.width());
    TEST_ASSERT_EQUAL(8, view.height());
    TEST_ASSERT_EQUAL_PTR(&fb, &view.surface());
}

TEST_CASE("gfx rotated cw90 maps viewer corners onto the turned surface", "[idfxx][gfx]") {
    // An 8x16 portrait surface mounted turned clockwise: its native
    // top-left sits at the viewer's top-right, its native bottom-left at
    // the viewer's top-left.
    auto fb = make_fb(8, 16);
    rotated view(fb, rotation::cw90);

    view.set_pixel(0, 0, true);  // viewer top-left -> native bottom-left
    view.set_pixel(15, 0, true); // viewer top-right -> native top-left
    view.set_pixel(0, 7, true);  // viewer bottom-left -> native bottom-right
    view.set_pixel(15, 7, true); // viewer bottom-right -> native top-right
    TEST_ASSERT_TRUE(fb.get_pixel(0, 15));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(7, 15));
    TEST_ASSERT_TRUE(fb.get_pixel(7, 0));
    TEST_ASSERT_EQUAL(4, count_set_pixels(fb));
}

TEST_CASE("gfx rotated ccw90 maps viewer corners onto the turned surface", "[idfxx][gfx]") {
    // The same surface mounted turned counter-clockwise: its native
    // top-right sits at the viewer's top-left.
    auto fb = make_fb(8, 16);
    rotated view(fb, rotation::ccw90);

    view.set_pixel(0, 0, true);  // viewer top-left -> native top-right
    view.set_pixel(15, 0, true); // viewer top-right -> native bottom-right
    view.set_pixel(0, 7, true);  // viewer bottom-left -> native top-left
    view.set_pixel(15, 7, true); // viewer bottom-right -> native bottom-left
    TEST_ASSERT_TRUE(fb.get_pixel(7, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(7, 15));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 15));
    TEST_ASSERT_EQUAL(4, count_set_pixels(fb));
}

TEST_CASE("gfx rotated view clips writes outside its swapped bounds", "[idfxx][gfx]") {
    auto fb = make_fb(8, 16);
    rotated view(fb, rotation::cw90);
    view.set_pixel(16, 0, true); // beyond the view's width (the surface's height)
    view.set_pixel(0, 8, true);  // beyond the view's height (the surface's width)
    view.set_pixel(SIZE_MAX, SIZE_MAX, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));

    rotated ccw(fb, rotation::ccw90);
    ccw.set_pixel(16, 0, true);
    ccw.set_pixel(0, 8, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

TEST_CASE("gfx rotated canvas renders the quarter-turned scene", "[idfxx][gfx]") {
    // Draw a glyph through a rotated canvas and check it against the same
    // glyph drawn natively and rotated by hand. The two mappings must agree
    // for every pixel of the frame.
    auto fb = make_fb(16, 32);
    rotated view(fb, rotation::cw90);
    canvas c(view);
    TEST_ASSERT_EQUAL(32, c.width());
    TEST_ASSERT_EQUAL(16, c.height());
    c.draw_text(spleen_8x16, 3, 1, "R");
    c.draw_hline(0, 0, 5, true);

    auto reference = make_fb(32, 16);
    draw_text(reference, spleen_8x16, 3, 1, "R");
    draw_hline(reference, 0, 0, 5, true);
    for (size_t y = 0; y < reference.height(); ++y) {
        for (size_t x = 0; x < reference.width(); ++x) {
            // Viewer (x, y) sits at native (y, height-1-x) under cw90.
            TEST_ASSERT_EQUAL(reference.get_pixel(x, y), fb.get_pixel(y, fb.height() - 1 - x));
        }
    }
}

TEST_CASE("gfx rotated fill and clear forward to the surface", "[idfxx][gfx]") {
    auto fb = make_fb(8, 16);
    rotated view(fb, rotation::ccw90);

    view.fill(true);
    TEST_ASSERT_EQUAL(8 * 16, count_set_pixels(fb));
    view.clear();
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));

    // Through a canvas too: a canvas that covers the whole view uses the
    // surface's native fill and clear.
    canvas c(view);
    c.fill(true);
    TEST_ASSERT_EQUAL(8 * 16, count_set_pixels(fb));
    c.clear();
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

// =============================================================================
// Runtime tests: render_banded
// =============================================================================

namespace {

// A 32x16 band surface.
struct test_band {
    using pixel_type = bool;

    std::array<std::array<bool, 32>, 16> pixels{};

    void set_pixel(size_t x, size_t y, bool on) noexcept {
        if (x < 32 && y < 16) {
            pixels[y][x] = on;
        }
    }
    [[nodiscard]] size_t width() const noexcept { return 32; }
    [[nodiscard]] size_t height() const noexcept { return 16; }
};

// A frame-sized pixel store that the band writes are assembled into. It
// stands in for a panel when testing try_render_banded end to end.
struct band_target {
    std::array<std::array<bool, 32>, 32> pixels{};
    size_t writes = 0;
    size_t fail_at = SIZE_MAX; // write index that reports failure

    [[nodiscard]] idfxx::result<void> try_write(const test_band& band, size_t x, size_t y) {
        if (writes++ == fail_at) {
            return idfxx::error(idfxx::errc::timeout);
        }
        for (size_t row = 0; row < 16; ++row) {
            for (size_t col = 0; col < 32; ++col) {
                pixels[y + row][x + col] = band.pixels[row][col];
            }
        }
        return {};
    }
};

} // namespace

TEST_CASE("gfx render_banded assembles a frame identical to a full-frame render", "[idfxx][gfx]") {
    auto reference = make_fb(32, 32);
    canvas ref_canvas(reference);
    draw_band_test_scene(ref_canvas);

    test_band band;
    band_target target;
    size_t passes = 0;
    auto rendered = try_render_banded(band, target, 32, [&](canvas<test_band>& c) {
        ++passes;
        draw_band_test_scene(c);
    });
    TEST_ASSERT_TRUE(rendered.has_value());
    TEST_ASSERT_EQUAL(2, passes);
    TEST_ASSERT_EQUAL(2, target.writes);

    for (size_t y = 0; y < 32; ++y) {
        for (size_t x = 0; x < 32; ++x) {
            TEST_ASSERT_EQUAL(reference.get_pixel(x, y), target.pixels[y][x]);
        }
    }
}

TEST_CASE("gfx render_banded rejects a frame height the band does not tile", "[idfxx][gfx]") {
    test_band band;
    band_target target;
    auto draw = [](canvas<test_band>&) {};

    auto not_multiple = try_render_banded(band, target, 24, draw);
    TEST_ASSERT_FALSE(not_multiple.has_value());
    TEST_ASSERT_TRUE(idfxx::errc::invalid_arg == not_multiple.error());

    auto zero = try_render_banded(band, target, 0, draw);
    TEST_ASSERT_FALSE(zero.has_value());
    TEST_ASSERT_TRUE(idfxx::errc::invalid_arg == zero.error());

    TEST_ASSERT_EQUAL(0, target.writes);
}

TEST_CASE("gfx render_banded stops and reports a failed write", "[idfxx][gfx]") {
    test_band band;
    band_target target;
    target.fail_at = 1; // second write fails

    size_t passes = 0;
    auto rendered = try_render_banded(band, target, 64, [&](canvas<test_band>&) { ++passes; });
    TEST_ASSERT_FALSE(rendered.has_value());
    TEST_ASSERT_TRUE(idfxx::errc::timeout == rendered.error());
    TEST_ASSERT_EQUAL(2, passes); // first band written, second failed, no further passes
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("gfx render_banded throws on error", "[idfxx][gfx]") {
    test_band band;
    band_target target;

    render_banded(band, target, 32, [](canvas<test_band>&) {}); // no throw on success

    bool thrown = false;
    try {
        render_banded(band, target, 24, [](canvas<test_band>&) {});
    } catch (const std::system_error&) {
        thrown = true;
    }
    TEST_ASSERT_TRUE(thrown);
}
#endif

// =============================================================================
// Runtime tests: text
// =============================================================================

TEST_CASE("gfx draw_text renders the glyph bitmap exactly", "[idfxx][gfx]") {
    auto fb = make_fb(32, 16);
    draw_text(fb, spleen_5x8, 2, 3, "A");
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 2, 3));
    TEST_ASSERT_TRUE(count_set_pixels(fb) > 0); // 'A' has ink
}

TEST_CASE("gfx draw_text advances one cell per character", "[idfxx][gfx]") {
    auto fb = make_fb(64, 16);
    draw_text(fb, spleen_5x8, 0, 0, "AB");
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 0, 0));
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'B', 5, 0));
}

TEST_CASE("gfx draw_text scale doubles every pixel", "[idfxx][gfx]") {
    auto fb = make_fb(64, 40);
    draw_text(fb, spleen_8x16, 4, 2, "7", true, 2);
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_8x16, '7', 4, 2, 2));
}

TEST_CASE("gfx draw_text with ink=false erases ink pixels", "[idfxx][gfx]") {
    auto fb = make_fb(32, 16);
    fb.fill(true);
    draw_text(fb, spleen_5x8, 0, 0, "A", false);
    for (size_t row = 0; row < spleen_5x8.height; ++row) {
        for (size_t col = 0; col < spleen_5x8.width; ++col) {
            // Ink pixels cleared; background pixels untouched (still set).
            TEST_ASSERT_EQUAL(!spleen_5x8.ink_at('A', col, row), fb.get_pixel(col, row));
        }
    }
}

TEST_CASE("gfx draw_text clips at framebuffer edges", "[idfxx][gfx]") {
    auto fb = make_fb(8, 8);
    draw_text(fb, spleen_8x16, 4, 4, "MMM"); // extends far past both edges
    // Must not crash. Pixels inside the fb can be set. There is nothing to
    // assert beyond bounds-safety (get_pixel outside is false by contract).
    TEST_ASSERT_FALSE(fb.get_pixel(8, 8));
}

TEST_CASE("gfx draw_text skips characters without glyphs", "[idfxx][gfx]") {
    auto fb = make_fb(32, 16);
    draw_text(fb, spleen_5x8, 0, 0, "\x01");
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
    // But the cursor still advances: 'A' after a missing glyph lands one cell in.
    draw_text(fb, spleen_5x8, 0, 0, "\x01\x41"); // \x41 == 'A'
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 5, 0));
}

TEST_CASE("gfx draw_text renders color ink on an rgb565 surface", "[idfxx][gfx]") {
    auto fb = make_color_fb(32, 16);
    constexpr rgb565 amber(255, 191, 0);
    draw_text(fb, spleen_5x8, 2, 3, "A", amber);

    size_t inked = 0;
    for (size_t row = 0; row < spleen_5x8.height; ++row) {
        for (size_t col = 0; col < spleen_5x8.width; ++col) {
            const bool ink = spleen_5x8.ink_at('A', col, row);
            const rgb565 expected = ink ? amber : rgb565{};
            TEST_ASSERT_TRUE(fb.get_pixel(2 + col, 3 + row) == expected);
            inked += ink ? 1 : 0;
        }
    }
    TEST_ASSERT_TRUE(inked > 0); // 'A' has ink
}

// =============================================================================
// Runtime tests: proportional text
// =============================================================================

namespace {

using idfxx::font::glyph_metrics;
using idfxx::font::glyph_range;
using idfxx::font::proportional_font;

// A tiny proportional font with ascent 3:
// - 'A' is a 2x2 block on the baseline.
// - 'g' is a 1x3 stroke that descends two rows below the baseline.
// - 'j' is a pixel that overhangs one column left of the pen.
// - U+00E9 is a pixel above the line top.
// - '?' (the replacement) is a pixel on the baseline row.
// - '-' is a 1x1 dash just above the baseline.
// - U+00AD is an inked glyph that must never be drawn.
// Its ink extends 4 rows above the baseline (one past the line top), 2 below
// it, and one column left of the pen.
constexpr uint8_t prop_bitmap[] = {
    0xC0, 0xC0,       // 'A'
    0x80, 0x80, 0x80, // 'g'
    0x80,             // 'j'
    0x80,             // U+00E9
    0x80,             // '?'
    0x80,             // '-'
    0x80,             // U+00AD
};
constexpr glyph_metrics prop_glyphs[] = {
    // ' '
    {.bitmap_offset = 0, .width = 0, .height = 0, .advance = 2, .x_offset = 0, .y_offset = 0},
    // '?'
    {.bitmap_offset = 7, .width = 1, .height = 1, .advance = 2, .x_offset = 0, .y_offset = -1},
    // 'A'
    {.bitmap_offset = 0, .width = 2, .height = 2, .advance = 3, .x_offset = 0, .y_offset = -2},
    // 'g'
    {.bitmap_offset = 2, .width = 1, .height = 3, .advance = 3, .x_offset = 1, .y_offset = -1},
    // 'j'
    {.bitmap_offset = 5, .width = 1, .height = 1, .advance = 2, .x_offset = -1, .y_offset = -1},
    // U+00E9
    {.bitmap_offset = 6, .width = 1, .height = 1, .advance = 2, .x_offset = 0, .y_offset = -4},
    // '-'
    {.bitmap_offset = 8, .width = 1, .height = 1, .advance = 2, .x_offset = 0, .y_offset = -2},
    // U+00AD
    {.bitmap_offset = 9, .width = 1, .height = 1, .advance = 2, .x_offset = 0, .y_offset = -1},
};
constexpr glyph_range prop_ranges[] = {
    {.first = U' ', .last = U' ', .first_glyph = 0},
    {.first = U'-', .last = U'-', .first_glyph = 6},
    {.first = U'?', .last = U'?', .first_glyph = 1},
    {.first = U'A', .last = U'A', .first_glyph = 2},
    {.first = U'g', .last = U'g', .first_glyph = 3},
    {.first = U'j', .last = U'j', .first_glyph = 4},
    {.first = U'\u00AD', .last = U'\u00AD', .first_glyph = 7},
    {.first = U'\u00E9', .last = U'\u00E9', .first_glyph = 5},
};
constexpr proportional_font prop_font{
    .ascent = 3,
    .descent = 2,
    .line_height = 6,
    .ink_ascent = 4,
    .ink_descent = 2,
    .min_left_bearing = -1,
    .min_right_bearing = 1,
    .ranges = prop_ranges,
    .glyphs = prop_glyphs,
    .replacement = &prop_glyphs[1],
    .bitmap = prop_bitmap,
};
constexpr proportional_font prop_font_no_replacement = [] {
    proportional_font font = prop_font;
    font.replacement = nullptr;
    return font;
}();

// Returns true when exactly the listed pixels are set.
bool only_pixels(
    const mono_framebuffer& fb, std::initializer_list<std::pair<size_t, size_t>> expected
) {
    for (const auto& [x, y] : expected) {
        if (!fb.get_pixel(x, y)) {
            return false;
        }
    }
    return count_set_pixels(fb) == expected.size();
}

// Returns true when tile holds the pixels of full whose top-left corner is (x0, y0).
template<typename Tile>
bool tile_matches(const mono_framebuffer& full, const Tile& tile, size_t x0, size_t y0) {
    for (size_t y = 0; y < tile.height(); ++y) {
        for (size_t x = 0; x < tile.width(); ++x) {
            if (full.get_pixel(x0 + x, y0 + y) != tile.get_pixel(x, y)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("gfx draw_text proportional sits glyphs on the baseline", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_text(fb, prop_font, 1, 0, "A"); // baseline at row 3: the block covers rows 1-2
    TEST_ASSERT_TRUE(only_pixels(fb, {{1, 1}, {2, 1}, {1, 2}, {2, 2}}));
}

TEST_CASE("gfx draw_text proportional advances per glyph", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_text(fb, prop_font, 0, 0, "A A");
    // 'A' advances 3 and ' ' 2, so the second 'A' starts at column 5.
    TEST_ASSERT_TRUE(
        only_pixels(fb, {{0, 1}, {1, 1}, {0, 2}, {1, 2}, {5, 1}, {6, 1}, {5, 2}, {6, 2}})
    );
    TEST_ASSERT_EQUAL(8, idfxx::font::text_width(prop_font, "A A"));
}

TEST_CASE("gfx draw_text proportional applies glyph offsets", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_text(fb, prop_font, 0, 0, "g"); // one column right, descending to row 4
    TEST_ASSERT_TRUE(only_pixels(fb, {{1, 2}, {1, 3}, {1, 4}}));
}

TEST_CASE("gfx draw_text proportional clips overhang at the left and top edges", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_text(fb, prop_font, 0, 0, "j"); // ink at column -1
    draw_text(fb, prop_font, 4, 0, "é"); // ink at row -1
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));

    draw_text(fb, prop_font, 1, 0, "j");
    draw_text(fb, prop_font, 4, 2, "é");
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 2}, {4, 1}}));
}

TEST_CASE("gfx draw_text proportional clips at the right and bottom edges", "[idfxx][gfx]") {
    auto fb = make_fb(4, 8);
    draw_text(fb, prop_font, 3, 0, "A"); // straddles the right edge
    draw_text(fb, prop_font, 0, 5, "g"); // descends past the bottom edge
    draw_text(fb, prop_font, 100, 100, "AAA");
    TEST_ASSERT_TRUE(only_pixels(fb, {{3, 1}, {3, 2}, {1, 7}}));
}

TEST_CASE("gfx draw_text proportional decodes UTF-8", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_text(fb, prop_font, 0, 2, "Aé"); // 'A' then the two-byte U+00E9 at column 3
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 3}, {1, 3}, {0, 4}, {1, 4}, {3, 1}}));
}

TEST_CASE("gfx draw_text proportional draws the replacement for missing code points",
          "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_text(fb, prop_font, 0, 0, "Z\xFF"); // missing, then malformed UTF-8
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 2}, {2, 2}}));
    TEST_ASSERT_EQUAL(4, idfxx::font::text_width(prop_font, "Z\xFF"));
}

TEST_CASE("gfx draw_text proportional skips missing code points without a replacement",
          "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_text(fb, prop_font_no_replacement, 0, 0, "ZA"); // 'Z' takes no space
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 1}, {1, 1}, {0, 2}, {1, 2}}));
    TEST_ASSERT_EQUAL(3, idfxx::font::text_width(prop_font_no_replacement, "ZA"));
}

TEST_CASE("gfx draw_text proportional with ink=false erases ink pixels", "[idfxx][gfx]") {
    auto fb = make_fb(4, 8);
    fb.fill(true);
    draw_text(fb, prop_font, 0, 0, "A", false);
    TEST_ASSERT_EQUAL(32 - 4, count_set_pixels(fb));
    TEST_ASSERT_FALSE(fb.get_pixel(0, 1));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
}

TEST_CASE("gfx draw_text proportional renders color ink", "[idfxx][gfx]") {
    auto fb = make_color_fb(8, 8);
    constexpr rgb565 amber(255, 191, 0);
    draw_text(fb, prop_font, 1, 0, "A", amber);
    TEST_ASSERT_TRUE(fb.get_pixel(1, 1) == amber);
    TEST_ASSERT_TRUE(fb.get_pixel(2, 2) == amber);
    TEST_ASSERT_TRUE(fb.get_pixel(0, 1) == rgb565{});
    TEST_ASSERT_TRUE(fb.get_pixel(1, 3) == rgb565{});
}

TEST_CASE("gfx draw_text proportional never draws soft hyphens", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    auto expected = make_fb(16, 8);
    // the soft hyphen's glyph has ink and an advance
    draw_text(fb, prop_font, 0, 0, "A\xC2\xAD" "A");
    draw_text(expected, prop_font, 0, 0, "AA");
    TEST_ASSERT_TRUE(same_pixels(expected, fb));
}

TEST_CASE("gfx canvas draw_text proportional renders bands identical to a full frame",
          "[idfxx][gfx]") {
    auto full = make_fb(16, 16);
    canvas(full).draw_text(prop_font, 1, 5, "Agéj"); // baseline at row 8

    // Two 8-row bands: glyphs straddling the band edge render their visible part in each.
    auto band = make_fb(16, 8);
    for (size_t y0 = 0; y0 < 16; y0 += 8) {
        band.clear();
        canvas(band, 0, y0, 16, 16).draw_text(prop_font, 1, 5, "Agéj");
        TEST_ASSERT_TRUE(band_matches(full, band, y0));
    }
    TEST_ASSERT_TRUE(count_set_pixels(full) > 0);
}

TEST_CASE("gfx clipping: proportional text above a band reaches no surface pixel", "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf, 0, 16, 8, 24);

    c.draw_text(prop_font, 0, 0, "AgAjé");
    draw_text(c, prop_font, 0, 0, "AgAjé", true);
    TEST_ASSERT_EQUAL(0, surf.writes);
}

TEST_CASE("gfx clipping: proportional text straddling a band edge writes only visible ink",
          "[idfxx][gfx]") {
    auto full = make_fb(16, 16);
    draw_text(full, prop_font, 1, 5, "Agéj");
    TEST_ASSERT_TRUE(count_set_pixels(full) > 0);

    for (size_t y0 = 0; y0 <= 12; ++y0) {
        sized_counter<16, 4> surf;
        canvas c(surf, 0, y0, 16, 16);
        c.draw_text(prop_font, 1, 5, "Agéj");
        TEST_ASSERT_EQUAL(count_set_rows(full, y0, y0 + 4), surf.writes);
    }
}

TEST_CASE("gfx clipping: proportional overhang crossing a band edge renders identically",
          "[idfxx][gfx]") {
    // 'é' rises 4 rows and 'j' hangs one column left, with the baseline on
    // or beside each band edge (rows 8, 16, 24).
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { c.draw_text(prop_font, 1, 5, "Agéj"); }));
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { c.draw_text(prop_font, 1, 12, "Agéj"); }));
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { c.draw_text(prop_font, 1, 21, "Agéj A"); }));
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { c.draw_text(prop_font, 1, 6, "Agéj"); }, 5));
}

TEST_CASE("gfx clipping: proportional accents reach a band above the line top", "[idfxx][gfx]") {
    // The band holds rows 8-15 and the line top is row 16, its bottom edge.
    // Only the é rises into the band, one row above the line top (column 3, row 15).
    auto band = make_fb(16, 8);
    canvas(band, 0, 8, 16, 24).draw_text(prop_font, 0, 16, "Aé");
    TEST_ASSERT_TRUE(only_pixels(band, {{3, 7}}));
}

TEST_CASE("gfx clipping: proportional left overhang reaches a clip left of the pen",
          "[idfxx][gfx]") {
    // The pen starts on the clip's right edge; the j's ink lies one column left of it.
    auto fb = make_fb(4, 8);
    draw_text(fb, prop_font, 4, 0, "j");
    TEST_ASSERT_TRUE(only_pixels(fb, {{3, 2}}));

    // Mid-string: after 'A', the pen reaches the edge at column 4.
    fb.clear();
    draw_text(fb, prop_font, 1, 0, "Aj");
    TEST_ASSERT_TRUE(only_pixels(fb, {{1, 1}, {2, 1}, {1, 2}, {2, 2}, {3, 2}}));

    // A band holding frame columns 4-7, with the pen on its right edge.
    auto band = make_fb(4, 8);
    canvas(band, 4, 0, 16, 8).draw_text(prop_font, 8, 0, "j");
    TEST_ASSERT_TRUE(only_pixels(band, {{3, 2}}));
}

TEST_CASE("gfx clipping: proportional text draws nothing in bands outside its ink rows",
          "[idfxx][gfx]") {
    // With the line top at row 8 (baseline row 11), the ink covers rows 7-12.
    // A font whose line box ends one row above the g's last ink row: ink may
    // reach past the line box, and the font's ink extents still cover it.
    constexpr proportional_font short_descent = [] {
        proportional_font font = prop_font;
        font.descent = 1;
        return font;
    }();
    for (const proportional_font* font : {&prop_font, &short_descent}) {
        // Bands just above and just below the ink rows.
        for (size_t y0 : {3, 13}) {
            sized_counter<16, 4> surf;
            canvas(surf, 0, y0, 16, 32).draw_text(*font, 1, 8, "Agéj A");
            TEST_ASSERT_EQUAL(0, surf.writes);
        }
        // One row inward, each band gets one pixel: the é (row 7), the g's tail (row 12).
        for (size_t y0 : {4, 12}) {
            sized_counter<16, 4> surf;
            canvas(surf, 0, y0, 16, 32).draw_text(*font, 1, 8, "Agéj A");
            TEST_ASSERT_EQUAL(1, surf.writes);
        }
    }
}

TEST_CASE("gfx clipping: proportional text in one-row and one-column bands equals a full render",
          "[idfxx][gfx]") {
    auto full = make_fb(16, 16);
    canvas(full).draw_text(prop_font, 0, 5, "Agéj A?");
    TEST_ASSERT_TRUE(count_set_pixels(full) > 0);

    for (size_t y0 = 0; y0 < 16; ++y0) {
        sized_counter<16, 1> band;
        canvas(band, 0, y0, 16, 16).draw_text(prop_font, 0, 5, "Agéj A?");
        TEST_ASSERT_TRUE(tile_matches(full, band, 0, y0));
    }
    for (size_t x0 = 0; x0 < 16; ++x0) {
        sized_counter<1, 16> band;
        canvas(band, x0, 0, 16, 16).draw_text(prop_font, 0, 5, "Agéj A?");
        TEST_ASSERT_TRUE(tile_matches(full, band, x0, 0));
    }
}

TEST_CASE("gfx clipping: proportional text past the clip's right edge draws its visible part",
          "[idfxx][gfx]") {
    auto full = make_fb(32, 16);
    draw_text(full, prop_font, 1, 5, "Agéj Agéj A");
    TEST_ASSERT_TRUE(count_set_pixels(full) > 0);

    // A surface narrower than the text, and a band ending mid-string.
    auto narrow = make_fb(8, 16);
    draw_text(narrow, prop_font, 1, 5, "Agéj Agéj A");
    TEST_ASSERT_TRUE(tile_matches(full, narrow, 0, 0));
    TEST_ASSERT_TRUE(count_set_pixels(narrow) > 0);

    auto band = make_fb(8, 16);
    canvas(band, 8, 0, 32, 16).draw_text(prop_font, 1, 5, "Agéj Agéj A");
    TEST_ASSERT_TRUE(tile_matches(full, band, 8, 0));
    TEST_ASSERT_TRUE(count_set_pixels(band) > 0);
}

// Draws a scene across a 24x8 rotated view whose backed columns end at x = 8 (cw90) or begin at
// x = 16 (ccw90): text, lines, rects and proportional text crossing and avoiding the boundary.
template<typename Surface>
void draw_rotated_band_scene(Surface& s) {
    canvas c(s);
    c.draw_text(spleen_5x8, 6, 0, "AB");
    c.draw_text(spleen_5x8, 14, 0, "CD");
    c.draw_text(spleen_5x8, 11, 0, "E", true, 2);
    c.draw_line(0, 0, 23, 7, true);
    c.draw_line(22, 1, 2, 6, true);
    c.draw_rect(4, 1, 16, 6, true);
    c.fill_rect(7, 2, 3, 2, true);
    c.draw_text(prop_font, 5, 5, "Agéj A");
    c.draw_text(prop_font, 13, 5, "Agéj A");
}

TEST_CASE("gfx clipping: rotated over a band canvas skips off-band work", "[idfxx][gfx]") {
    for (const auto dir : {rotation::cw90, rotation::ccw90}) {
        // The view is 24 wide and 8 high; the surface backs only 8 of its columns.
        const size_t off = dir == rotation::cw90 ? 8 : 0; // first unbacked column
        const size_t on = dir == rotation::cw90 ? 0 : 16; // first backed column
        sized_counter<8, 8> surf;
        canvas inner(surf, 0, 16, 8, 24);
        rotated view(inner, dir);
        TEST_ASSERT_EQUAL(24, view.width());
        TEST_ASSERT_EQUAL(8, view.height());

        draw_text(view, spleen_5x8, off, 0, "ABC", true, 1);
        draw_text(view, spleen_5x8, off, 0, "A", true, 2);
        draw_text(view, prop_font, off, 4, "Agéj A");
        fill_rect(view, off, 0, 16, 8, true);
        draw_line(view, off, 0, off + 15, 7, true);
        draw_line(view, off + 15, 7, off, 0, true);
        draw_rect(view, off, 0, 16, 8, true);
        TEST_ASSERT_EQUAL(0, surf.writes);

        canvas c(view);
        c.draw_text(spleen_5x8, off, 0, "ABC");
        c.draw_text(spleen_5x8, off, 0, "A", true, 2);
        c.draw_text(prop_font, off, 4, "Agéj A");
        c.fill_rect(off, 0, 16, 8, true);
        c.draw_line(off, 0, off + 15, 7, true);
        c.draw_line(off + 15, 7, off, 0, true);
        c.draw_rect(off, 0, 16, 8, true);
        TEST_ASSERT_EQUAL(0, surf.writes);

        // Sanity: a fill wholly in the backed columns writes exactly its area.
        c.fill_rect(on + 2, 1, 4, 3, true);
        TEST_ASSERT_EQUAL(12, surf.writes);
    }
}

TEST_CASE("gfx clipping: rotated over a band canvas renders the same pixels", "[idfxx][gfx]") {
    for (const auto dir : {rotation::cw90, rotation::ccw90}) {
        auto full = make_fb(8, 24);
        canvas rc(full);
        rotated rv(rc, dir);
        draw_rotated_band_scene(rv);
        TEST_ASSERT_TRUE(count_set_pixels(full) > 0);

        auto band = make_fb(8, 8);
        canvas bc(band, 0, 16, 8, 24);
        rotated bv(bc, dir);
        draw_rotated_band_scene(bv);
        TEST_ASSERT_TRUE(count_set_pixels(band) > 0);
        TEST_ASSERT_TRUE(band_matches(full, band, 16));
    }
}

TEST_CASE("gfx clipping: canvas draw_rect clips through fill_rect", "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf, 0, 4, 8, 12);

    // A rect wholly above the band reaches no pixel.
    c.draw_rect(0, 0, 8, 4, true);
    TEST_ASSERT_EQUAL(0, surf.writes);

    // The left and right edges of rows 4-6 plus the full bottom row 7.
    c.draw_rect(0, 0, 8, 8, true);
    TEST_ASSERT_EQUAL(3 * 2 + 8, surf.writes);
}

// =============================================================================
// Runtime tests: text boxes
// =============================================================================

TEST_CASE("gfx draw_text_box wraps lines one line_height apart", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    auto expected = make_fb(16, 16);
    // 'A' advances 3 and ' ' 2: "A A" is 8 wide, so a 5-wide box takes one 'A' per line.
    TEST_ASSERT_EQUAL(12, draw_text_box(fb, prop_font, 1, 2, 5, "A A"));
    draw_text(expected, prop_font, 1, 2, "A");
    draw_text(expected, prop_font, 1, 8, "A");
    TEST_ASSERT_TRUE(same_pixels(expected, fb));

    fb.clear();
    TEST_ASSERT_EQUAL(6, draw_text_box(fb, prop_font, 1, 2, 8, "A A"));
    TEST_ASSERT_EQUAL(0, draw_text_box(fb, prop_font, 1, 2, 8, ""));
}

TEST_CASE("gfx draw_text_box aligns lines within the box", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    auto expected = make_fb(16, 16);
    // Lines 3 ("A") and 8 ("A A") wide in a 9-wide box at column 2.
    TEST_ASSERT_EQUAL(12, draw_text_box(fb, prop_font, 2, 0, 9, "A\nA A", text_align::left));
    draw_text(expected, prop_font, 2, 0, "A");
    draw_text(expected, prop_font, 2, 6, "A A");
    TEST_ASSERT_TRUE(same_pixels(expected, fb));

    fb.clear();
    expected.clear();
    draw_text_box(fb, prop_font, 2, 0, 9, "A\nA A", text_align::center);
    draw_text(expected, prop_font, 5, 0, "A");   // 2 + (9 - 3) / 2
    draw_text(expected, prop_font, 2, 6, "A A"); // 2 + (9 - 8) / 2
    TEST_ASSERT_TRUE(same_pixels(expected, fb));

    fb.clear();
    expected.clear();
    draw_text_box(fb, prop_font, 2, 0, 9, "A\nA A", text_align::right);
    draw_text(expected, prop_font, 8, 0, "A");   // 2 + 9 - 3
    draw_text(expected, prop_font, 3, 6, "A A"); // 2 + 9 - 8
    TEST_ASSERT_TRUE(same_pixels(expected, fb));
}

TEST_CASE("gfx draw_text_box starts lines wider than the box at its left edge", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    auto expected = make_fb(16, 16);
    // Each 3-wide 'A' gets its own line in a 2-wide box, however aligned.
    TEST_ASSERT_EQUAL(12, draw_text_box(fb, prop_font, 4, 0, 2, "AA", text_align::right));
    draw_text(expected, prop_font, 4, 0, "A");
    draw_text(expected, prop_font, 4, 6, "A");
    TEST_ASSERT_TRUE(same_pixels(expected, fb));

    fb.clear();
    draw_text_box(fb, prop_font, 4, 0, 2, "AA", text_align::center);
    TEST_ASSERT_TRUE(same_pixels(expected, fb));
}

TEST_CASE("gfx draw_text_box draws a hyphen at a soft-hyphen break", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    auto expected = make_fb(16, 16);
    // "AA" (6) plus '-' (2) fits an 8-wide box; "AAA" (9) does not.
    TEST_ASSERT_EQUAL(12, draw_text_box(fb, prop_font, 0, 0, 8, "AA\xC2\xAD" "A"));
    draw_text(expected, prop_font, 0, 0, "AA-");
    draw_text(expected, prop_font, 0, 6, "A");
    TEST_ASSERT_TRUE(same_pixels(expected, fb));

    // Right-aligned, the hyphen counts toward the line's width.
    fb.clear();
    expected.clear();
    draw_text_box(fb, prop_font, 0, 0, 10, "AA\xC2\xAD" "AA", text_align::right);
    draw_text(expected, prop_font, 2, 0, "AA-");
    draw_text(expected, prop_font, 4, 6, "AA");
    TEST_ASSERT_TRUE(same_pixels(expected, fb));
}

TEST_CASE("gfx draw_text_box renders color ink", "[idfxx][gfx]") {
    auto fb = make_color_fb(8, 16);
    constexpr rgb565 amber(255, 191, 0);
    TEST_ASSERT_EQUAL(12, draw_text_box(fb, prop_font, 0, 0, 5, "A A", amber, text_align::right));
    TEST_ASSERT_TRUE(fb.get_pixel(2, 1) == amber); // 'A' at column 2 = 0 + 5 - 3
    TEST_ASSERT_TRUE(fb.get_pixel(3, 8) == amber);
    TEST_ASSERT_TRUE(fb.get_pixel(1, 1) == rgb565{});
}

TEST_CASE("gfx canvas draw_text_box draws like the free functions", "[idfxx][gfx]") {
    auto fb = make_fb(32, 32);
    auto expected = make_fb(32, 32);
    canvas c(fb);
    TEST_ASSERT_EQUAL(12, c.draw_text_box(prop_font, 1, 0, 5, "A A", text_align::center));
    TEST_ASSERT_EQUAL(12, c.draw_text_box(prop_font, 1, 12, 5, "A A", true, text_align::right));
    TEST_ASSERT_EQUAL(8, c.draw_text_box(spleen_5x8, 0, 24, 12, "AB", text_align::right));
    TEST_ASSERT_EQUAL(16, c.draw_text_box(spleen_5x8, 12, 16, 10, "A", true, text_align::left, 2));
    draw_text_box(expected, prop_font, 1, 0, 5, "A A", text_align::center);
    draw_text_box(expected, prop_font, 1, 12, 5, "A A", true, text_align::right);
    draw_text_box(expected, spleen_5x8, 0, 24, 12, "AB", text_align::right);
    draw_text_box(expected, spleen_5x8, 12, 16, 10, "A", true, text_align::left, 2);
    TEST_ASSERT_TRUE(same_pixels(expected, fb));
    TEST_ASSERT_TRUE(count_set_pixels(fb) > 0);

    // A translated canvas lays the box out in canvas coordinates, clipping to its band.
    auto full = make_fb(16, 16);
    canvas(full).draw_text_box(prop_font, 1, 0, 5, "A A");
    auto band = make_fb(16, 8);
    canvas(band, 0, 4, 16, 16).draw_text_box(prop_font, 1, 0, 5, "A A");
    TEST_ASSERT_TRUE(band_matches(full, band, 4));
    TEST_ASSERT_TRUE(count_set_pixels(band) > 0);
}

TEST_CASE("gfx clipping: text boxes above a band reach no surface pixel", "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf, 0, 16, 8, 24);

    c.draw_text_box(prop_font, 0, 0, 5, "A A"); // two lines, rows 0-11
    c.draw_text_box(spleen_5x8, 0, 0, 12, "AB CD");
    c.draw_text_box(spleen_5x8, 0, 0, 20, "AB", true, text_align::center, 2);
    draw_text_box(c, prop_font, 0, 0, 5, "A A");
    TEST_ASSERT_EQUAL(0, surf.writes);
}

TEST_CASE("gfx clipping: draw_text_box returns the same height in every band", "[idfxx][gfx]") {
    auto full = make_fb(32, 32);
    canvas full_canvas(full);
    const size_t prop_height = 
        full_canvas.draw_text_box(prop_font, 1, 3, 12, "A A A A", text_align::center);
    const size_t mono_height = 
        full_canvas.draw_text_box(spleen_5x8, 0, 12, 20, "AB CD EF", true, text_align::right, 2);
    TEST_ASSERT_TRUE(prop_height > 0);
    TEST_ASSERT_TRUE(mono_height > 0);

    for (size_t y0 = 0; y0 < 32; y0 += 8) {
        auto band = make_fb(32, 8);
        canvas c(band, 0, y0, 32, 32);
        TEST_ASSERT_EQUAL(
            prop_height, c.draw_text_box(prop_font, 1, 3, 12, "A A A A", text_align::center)
        );
        TEST_ASSERT_EQUAL(
            mono_height,
            c.draw_text_box(spleen_5x8, 0, 12, 20, "AB CD EF", true, text_align::right, 2)
        );
        TEST_ASSERT_TRUE(band_matches(full, band, y0));
    }

    // A box wholly outside the band still reports its full height.
    sized_counter<8, 8> surf;
    canvas above(surf, 0, 16, 8, 24);
    TEST_ASSERT_EQUAL(
        prop_height, above.draw_text_box(prop_font, 1, 3, 12, "A A A A", text_align::center)
    );
    TEST_ASSERT_EQUAL(0, surf.writes);
}

TEST_CASE("gfx draw_text_box wraps mono text in scaled cells", "[idfxx][gfx]") {
    auto fb = make_fb(32, 48);
    // 5x8 cells: "AB" is 10 wide, so a 12-wide box breaks "AB CD" after the space.
    TEST_ASSERT_EQUAL(16, draw_text_box(fb, spleen_5x8, 0, 0, 12, "AB CD", text_align::right));
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 2, 0));
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'B', 7, 0));
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'C', 2, 8));
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'D', 7, 8));

    // At scale 2 lines are 16 rows apart and widths double: "AB" is 20 wide, centered in 30.
    fb.clear();
    TEST_ASSERT_EQUAL(
        32, draw_text_box(fb, spleen_5x8, 0, 16, 30, "AB CD", true, text_align::center, 2)
    );
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'A', 5, 16, 2));
    TEST_ASSERT_TRUE(matches_glyph(fb, spleen_5x8, 'C', 5, 32, 2));
    TEST_ASSERT_EQUAL(0, draw_text_box(fb, spleen_5x8, 0, 0, 30, "", true));
}

// =============================================================================
// to_string / formatter tests
// =============================================================================

TEST_CASE("rotation to_string and formatter", "[idfxx][gfx]") {
    TEST_ASSERT_EQUAL_STRING("cw90", idfxx::to_string(rotation::cw90).c_str());
    TEST_ASSERT_EQUAL_STRING("ccw90", idfxx::to_string(rotation::ccw90).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(99)", idfxx::to_string(static_cast<rotation>(99)).c_str());
#ifdef CONFIG_IDFXX_STD_FORMAT
    auto s = std::format("{}", rotation::cw90);
    TEST_ASSERT_EQUAL_STRING("cw90", s.c_str());
#endif
}

TEST_CASE("text_align to_string and formatter", "[idfxx][gfx]") {
    TEST_ASSERT_EQUAL_STRING("left", idfxx::to_string(text_align::left).c_str());
    TEST_ASSERT_EQUAL_STRING("center", idfxx::to_string(text_align::center).c_str());
    TEST_ASSERT_EQUAL_STRING("right", idfxx::to_string(text_align::right).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(99)", idfxx::to_string(static_cast<text_align>(99)).c_str());
#ifdef CONFIG_IDFXX_STD_FORMAT
    auto s = std::format("{}", text_align::center);
    TEST_ASSERT_EQUAL_STRING("center", s.c_str());
#endif
}

// =============================================================================
// Runtime tests: drawing-space canvas (explicit width and height)
// =============================================================================

TEST_CASE("gfx canvas with a drawing space reports the space size", "[idfxx][gfx]") {
    auto fb = make_fb(8, 8);
    canvas c(fb, 0, 16, 8, 32);
    TEST_ASSERT_EQUAL(8, c.width());
    TEST_ASSERT_EQUAL(32, c.height());

    c.set_pixel(0, 16, true); // surface (0, 0)
    c.set_pixel(7, 23, true); // surface (7, 7)
    c.set_pixel(0, 15, true); // above the surface
    c.set_pixel(0, 24, true); // below the surface, inside the space
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(7, 7));
    TEST_ASSERT_EQUAL(2, count_set_pixels(fb));
}

TEST_CASE("gfx clipping: a surface extending past the drawing space is clipped to it",
          "[idfxx][gfx]") {
    auto fb = make_fb(8, 8);
    canvas c(fb, 0, 4, 8, 8); // space rows 4-7 land on band rows 0-3; band rows 4-7 are outside
    c.fill_rect(0, 0, 8, 12, true);
    TEST_ASSERT_EQUAL(8 * 4, count_set_rows(fb, 0, 4));
    TEST_ASSERT_EQUAL(0, count_set_rows(fb, 4, 8));

    c.clear();
    c.fill(true);
    TEST_ASSERT_EQUAL(8 * 4, count_set_rows(fb, 0, 4));
    TEST_ASSERT_EQUAL(0, count_set_rows(fb, 4, 8));

    sized_counter<8, 8> surf;
    canvas cc(surf, 0, 4, 8, 8);
    cc.draw_text(spleen_5x8, 0, 8, "ABC");
    cc.draw_text(spleen_5x8, 0, 8, "ABC", true, 2);
    cc.fill_rect(0, 8, 8, 4, true);
    cc.draw_rect(0, 8, 8, 4, true);
    cc.draw_line(0, 8, 7, 11, true);
    cc.draw_line(7, 11, 0, 8, true);
    TEST_ASSERT_EQUAL(0, surf.writes);

    // Sanity: content inside both the space and the surface writes.
    cc.fill_rect(0, 4, 8, 1, true);
    TEST_ASSERT_EQUAL(8, surf.writes);
}

TEST_CASE("gfx clipping: a window on a drawing-space canvas clamps to the space",
          "[idfxx][gfx]") {
    auto fb = make_fb(8, 8);
    canvas c(fb, 0, 4, 8, 8);

    auto w = c.window(2, 6, 20, 20);
    TEST_ASSERT_EQUAL(6, w.width());
    TEST_ASSERT_EQUAL(2, w.height());
    w.fill(true);
    // Space rows 6-7 are band rows 2-3, columns 2-7.
    TEST_ASSERT_EQUAL(6 * 2, count_set_pixels(fb));
    TEST_ASSERT_EQUAL(6 * 2, count_set_rows(fb, 2, 4));

    auto empty = c.window(0, 8, 8, 4); // wholly outside the space
    TEST_ASSERT_EQUAL(0, empty.height());
    empty.fill(true);
    TEST_ASSERT_EQUAL(6 * 2, count_set_pixels(fb));
}

TEST_CASE("gfx clipping: below-band content on a full-height canvas writes nothing",
          "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf, 0, 0, 8, 32); // the surface backs rows 0-7 of a 32-row space

    c.draw_text(spleen_5x8, 0, 8, "ABC");
    c.draw_text(spleen_5x8, 0, 8, "ABC", true, 2);
    c.draw_text(spleen_8x16, 0, 16, "A");
    c.draw_text(prop_font, 0, 16, "Ag");
    c.draw_text_box(spleen_5x8, 0, 8, 8, "AB CD");
    c.draw_text_box(prop_font, 0, 16, 8, "A A A");
    c.fill_rect(0, 8, 8, 16, true);
    c.draw_rect(0, 8, 8, 16, true);
    c.draw_line(0, 8, 7, 31, true);
    c.draw_line(7, 31, 0, 8, true);
    c.draw_hline(0, 20, 8, true);
    c.draw_vline(3, 8, 24, true);
    draw_text(c, spleen_5x8, 0, 8, "ABC", true, 1);
    draw_text(c, prop_font, 0, 16, "A", true);
    draw_text_box(c, spleen_5x8, 0, 8, 8, "AB CD", true);
    draw_text_box(c, prop_font, 0, 16, 8, "A A", true);
    fill_rect(c, 0, 8, 8, 16, true);
    draw_rect(c, 0, 8, 8, 16, true);
    draw_line(c, 0, 8, 7, 31, true);
    TEST_ASSERT_EQUAL(0, surf.writes);

    // Sanity: content reaching the band does write, for both font kinds.
    c.draw_text(spleen_5x8, 0, 0, "A");
    TEST_ASSERT_TRUE(surf.writes > 0);
    surf.writes = 0;
    c.draw_text(prop_font, 0, 3, "A");
    TEST_ASSERT_EQUAL(4, surf.writes);
    surf.writes = 0;
    c.fill_rect(0, 7, 8, 5, true); // only row 7 is in the band
    TEST_ASSERT_EQUAL(8, surf.writes);
}

// =============================================================================
// Runtime tests: render_banded canvas size
// =============================================================================

namespace {

// Draws a footer positioned from the canvas height, plus a header, so the
// scene only assembles correctly when height() is the frame height in
// every band.
template<typename Canvas>
void draw_footer_scene(Canvas& c) {
    c.draw_text(spleen_5x8, 2, 2, "TOP");
    c.fill_rect(0, c.height() - 3, c.width(), 3, true);
    c.draw_text(spleen_5x8, 4, c.height() - 10, "END");
    c.draw_rect(0, 0, c.width(), c.height(), true);
}

// Draws a scene in a canvas over a rotated view, in the rotated space.
template<typename Canvas>
void draw_rotated_scene(Canvas& lc) {
    lc.draw_text(spleen_5x8, 2, 4, "RT");
    lc.draw_text(prop_font, 2, 20, "Ag A");
    lc.draw_line(0, 0, 31, 31, true);
    lc.fill_rect(20, 8, 6, 12, true);
    lc.draw_rect(1, 1, 30, 30, true);
}

// Returns true when the assembled target equals the frame.
bool target_matches(const band_target& target, const mono_framebuffer& frame) {
    for (size_t y = 0; y < 32; ++y) {
        for (size_t x = 0; x < 32; ++x) {
            if (frame.get_pixel(x, y) != target.pixels[y][x]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("gfx render_banded canvas reports the frame size in every pass", "[idfxx][gfx]") {
    test_band band;
    band_target target;
    size_t passes = 0;
    size_t wrong_height = 0;
    size_t wrong_width = 0;
    auto rendered = try_render_banded(band, target, 32, [&](canvas<test_band>& c) {
        ++passes;
        wrong_height += c.height() != 32;
        wrong_width += c.width() != 32;
    });
    TEST_ASSERT_TRUE(rendered.has_value());
    TEST_ASSERT_EQUAL(2, passes);
    TEST_ASSERT_EQUAL(0, wrong_height);
    TEST_ASSERT_EQUAL(0, wrong_width);
}

TEST_CASE("gfx render_banded footer placed from canvas height matches a full frame",
          "[idfxx][gfx]") {
    auto reference = make_fb(32, 32);
    canvas ref_canvas(reference);
    draw_footer_scene(ref_canvas);
    TEST_ASSERT_TRUE(count_set_pixels(reference) > 0);

    test_band band;
    band_target target;
    auto rendered = try_render_banded(band, target, 32, [&](canvas<test_band>& c) {
        draw_footer_scene(c);
    });
    TEST_ASSERT_TRUE(rendered.has_value());
    TEST_ASSERT_TRUE(target_matches(target, reference));
}

TEST_CASE("gfx render_banded rotated view over the canvas matches a full frame",
          "[idfxx][gfx]") {
    for (rotation rot : {rotation::cw90, rotation::ccw90}) {
        auto reference = make_fb(32, 32);
        canvas ref_canvas(reference);
        rotated ref_view(ref_canvas, rot);
        canvas ref_lc(ref_view);
        draw_rotated_scene(ref_lc);
        TEST_ASSERT_TRUE(count_set_pixels(reference) > 0);

        test_band band;
        band_target target;
        auto rendered = try_render_banded(band, target, 32, [&](canvas<test_band>& c) {
            rotated v(c, rot);
            canvas lc(v);
            draw_rotated_scene(lc);
        });
        TEST_ASSERT_TRUE(rendered.has_value());
        TEST_ASSERT_TRUE(target_matches(target, reference));
    }
}

// =============================================================================
// Runtime tests: signed positions
// =============================================================================

TEST_CASE("gfx rectangles and lines partly off the left and top draw their visible part",
          "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    fill_rect(fb, -2, -3, 5, 6, true); // columns 0-2, rows 0-2
    TEST_ASSERT_EQUAL(3 * 3, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(2, 2));
    TEST_ASSERT_FALSE(fb.get_pixel(3, 0));
    TEST_ASSERT_FALSE(fb.get_pixel(0, 3));

    fb.clear();
    draw_hline(fb, -4, 5, 6, true);   // columns 0-1 of row 5
    draw_vline(fb, 7, -10, 12, true); // rows 0-1 of column 7
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 5}, {1, 5}, {7, 0}, {7, 1}}));

    // The outline's top and left edges fall off the surface; its bottom
    // (row 2, columns 0-3) and right (column 3, rows 0-1) edges remain.
    fb.clear();
    draw_rect(fb, -2, -2, 6, 5, true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 2}, {1, 2}, {2, 2}, {3, 2}, {3, 0}, {3, 1}}));

    // The canvas members clip the same way.
    auto via_canvas = make_fb(16, 16);
    canvas c(via_canvas);
    c.draw_rect(-2, -2, 6, 5, true);
    TEST_ASSERT_TRUE(same_pixels(fb, via_canvas));
    via_canvas.clear();
    c.fill_rect(-2, -3, 5, 6, true);
    c.draw_hline(-4, 5, 6, true);
    c.draw_vline(7, -10, 12, true);
    TEST_ASSERT_EQUAL(3 * 3 + 2 + 2, count_set_pixels(via_canvas));
    TEST_ASSERT_TRUE(via_canvas.get_pixel(2, 2));
    TEST_ASSERT_TRUE(via_canvas.get_pixel(1, 5));
    TEST_ASSERT_TRUE(via_canvas.get_pixel(7, 1));
}

TEST_CASE("gfx draw_line with negative endpoints draws the same pixels as unclipped",
          "[idfxx][gfx]") {
    // Each line is drawn on a 48x48 frame with both endpoints on it, and
    // again on a 16x16 surface whose (0, 0) is frame (16, 16), so endpoints
    // left of or above the surface are negative. The surface must hold
    // exactly its part of the frame, for lines of every slope and direction.
    auto full = make_fb(48, 48);
    auto part = make_fb(16, 16);
    canvas c(part);
    uint32_t state = 1;
    const auto next = [&state] {
        state = state * 1103515245u + 12345u;
        return static_cast<int>((state >> 16) % 40) + 4; // [4, 44)
    };
    size_t crossing = 0;
    for (int i = 0; i < 400; ++i) {
        const int x0 = next(), y0 = next(), x1 = next(), y1 = next();
        full.clear();
        part.clear();
        draw_line(full, x0, y0, x1, y1, true);
        if (i % 2 == 0) {
            draw_line(part, x0 - 16, y0 - 16, x1 - 16, y1 - 16, true);
        } else {
            c.draw_line(x0 - 16, y0 - 16, x1 - 16, y1 - 16, true);
        }
        TEST_ASSERT_TRUE(tile_matches(full, part, 16, 16));
        crossing += count_set_pixels(part) > 0 ? 1 : 0;
    }
    TEST_ASSERT_TRUE(crossing > 50); // plenty of the lines cross the surface
}

TEST_CASE("gfx mono text at negative positions clips glyph columns and rows", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);

    // Two columns left of the surface: glyph columns 2-4 land on columns 0-2.
    draw_text(fb, spleen_5x8, -2, 0, "A");
    size_t ink = 0;
    for (size_t row = 0; row < spleen_5x8.height; ++row) {
        for (size_t col = 2; col < spleen_5x8.width; ++col) {
            TEST_ASSERT_EQUAL(spleen_5x8.ink_at('A', col, row), fb.get_pixel(col - 2, row));
            ink += spleen_5x8.ink_at('A', col, row) ? 1 : 0;
        }
    }
    TEST_ASSERT_TRUE(ink > 0);
    TEST_ASSERT_EQUAL(ink, count_set_pixels(fb));

    // Three rows above the surface: glyph rows 3-7 land on rows 0-4.
    fb.clear();
    draw_text(fb, spleen_5x8, 0, -3, "A");
    ink = 0;
    for (size_t row = 3; row < spleen_5x8.height; ++row) {
        for (size_t col = 0; col < spleen_5x8.width; ++col) {
            TEST_ASSERT_EQUAL(spleen_5x8.ink_at('A', col, row), fb.get_pixel(col, row - 3));
            ink += spleen_5x8.ink_at('A', col, row) ? 1 : 0;
        }
    }
    TEST_ASSERT_TRUE(ink > 0);
    TEST_ASSERT_EQUAL(ink, count_set_pixels(fb));

    // Scaled text and a text box starting off both edges match a frame
    // drawn with the same text wholly on it.
    auto full = make_fb(48, 48);
    draw_text(full, spleen_5x8, 12, 11, "AB", true, 3);
    draw_text_box(full, spleen_5x8, 9, 30, 30, "AB CD EF", true, text_align::center);
    fb.clear();
    draw_text(fb, spleen_5x8, 12 - 16, 11 - 16, "AB", true, 3); // a block straddles column 0
    const size_t height =
        draw_text_box(fb, spleen_5x8, 9 - 16, 30 - 16, 30, "AB CD EF", true, text_align::center);
    TEST_ASSERT_EQUAL(2 * spleen_5x8.height, height);
    TEST_ASSERT_TRUE(count_set_pixels(fb) > 0);
    TEST_ASSERT_TRUE(tile_matches(full, fb, 16, 16));
}

TEST_CASE("gfx proportional text at negative positions draws its visible part", "[idfxx][gfx]") {
    auto full = make_fb(32, 32);
    draw_text(full, prop_font, 7, 6, "AgjAé");
    draw_text_box(full, prop_font, 5, 14, 12, "A A­A j", true, text_align::right);

    auto fb = make_fb(16, 16);
    draw_text(fb, prop_font, 7 - 8, 6 - 8, "AgjAé");
    draw_text_box(fb, prop_font, 5 - 8, 14 - 8, 12, "A A­A j", true, text_align::right);
    TEST_ASSERT_TRUE(count_set_pixels(fb) > 0);
    TEST_ASSERT_TRUE(tile_matches(full, fb, 8, 8));

    // Ink entirely left of or above the surface writes nothing.
    sized_counter<8, 8> surf;
    canvas c(surf);
    c.draw_text(prop_font, -8, 0, "AAA"); // the last 'A' ends at column -1
    c.draw_text(prop_font, 0, -5, "Ag");  // 'g' descends to row -1
    TEST_ASSERT_EQUAL(0, surf.writes);
}

TEST_CASE("gfx drawing wholly off the left or top writes nothing", "[idfxx][gfx]") {
    sized_counter<8, 8> surf;
    canvas c(surf);
    c.fill_rect(-10, 0, 10, 8, true); // ends at column -1
    c.fill_rect(0, -5, 8, 5, true);   // ends at row -1
    c.fill_rect(-10, -10, 10, 20, true);
    c.draw_rect(-20, -20, 10, 10, true);
    c.draw_rect(-1, -1, 10, 10, true); // its border runs just outside the surface
    c.draw_hline(-8, 3, 8, true);
    c.draw_vline(3, -8, 8, true);
    c.draw_line(-10, -1, -1, -10, true);
    c.draw_line(-5, 4, -1, 4, true);
    c.draw_line(-2, 1, 8, -9, true); // passes just outside the corner
    c.draw_text(spleen_5x8, -5, 0, "A");
    c.draw_text(spleen_5x8, -20, 0, "AB", true, 2);
    c.draw_text(spleen_5x8, 0, -8, "A");
    c.draw_text_box(spleen_5x8, 0, -16, 8, "A B", true);
    c.window(-8, -8, 8, 8).fill(true);
    c.window(-4, 0, 4, 8).draw_text(spleen_5x8, 0, 0, "A");
    TEST_ASSERT_EQUAL(0, surf.writes);
}

TEST_CASE("gfx positions at the int limits do not overflow", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);

    // A rectangle from INT_MIN spanning SIZE_MAX covers the whole surface.
    fill_rect(fb, INT_MIN, INT_MIN, SIZE_MAX, SIZE_MAX, true);
    TEST_ASSERT_EQUAL(16 * 16, count_set_pixels(fb));

    fb.clear();
    fill_rect(fb, INT_MAX, INT_MAX, SIZE_MAX, SIZE_MAX, true);
    fill_rect(fb, INT_MIN, 0, 16, 16, true);
    draw_rect(fb, INT_MIN, INT_MIN, SIZE_MAX, SIZE_MAX, true); // every edge off the surface
    draw_rect(fb, INT_MAX - 1, INT_MAX - 1, SIZE_MAX, SIZE_MAX, true);
    draw_hline(fb, INT_MAX, 0, SIZE_MAX, true);
    draw_vline(fb, 0, INT_MIN, 16, true);
    draw_text(fb, spleen_5x8, INT_MAX, INT_MAX, "AB", true, 4);
    draw_text(fb, spleen_5x8, INT_MIN, 0, "ABCDEFGH", true, 4);
    draw_text(fb, spleen_5x8, 0, INT_MIN, "A", true, 4);
    draw_text(fb, prop_font, INT_MAX, INT_MAX, "Ag");
    draw_text(fb, prop_font, INT_MAX, 0, "j");
    draw_text(fb, prop_font, INT_MIN, INT_MIN, "Aé");
    // Text boxes whose lines and hyphens land past INT_MAX still report
    // the height of every line.
    TEST_ASSERT_EQUAL(
        4 * spleen_5x8.height, draw_text_box(fb, spleen_5x8, 0, INT_MAX - 7, 5, "A A A A", true)
    );
    TEST_ASSERT_EQUAL(
        2 * prop_font.line_height,
        draw_text_box(fb, prop_font, INT_MAX, 0, 8, "AA­AA", true, text_align::right)
    );
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));

    // Lines between endpoints at the int limits draw exactly their visible
    // part, without walking the whole line.
    draw_line(fb, INT_MIN, INT_MIN, INT_MAX, INT_MAX, true); // the diagonal x == y
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_TRUE(fb.get_pixel(i, i));
    }
    TEST_ASSERT_EQUAL(16, count_set_pixels(fb));

    fb.clear();
    draw_line(fb, INT_MAX, 5, INT_MIN, 5, true);
    draw_line(fb, 9, INT_MIN, 9, INT_MAX, true);
    TEST_ASSERT_EQUAL(16 + 15, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 5));
    TEST_ASSERT_TRUE(fb.get_pixel(15, 5));
    TEST_ASSERT_TRUE(fb.get_pixel(9, 0));
    TEST_ASSERT_TRUE(fb.get_pixel(9, 15));

    // A shallow line spanning the whole int range: one pixel per column.
    fb.clear();
    draw_line(fb, INT_MIN, 0, INT_MAX, 15, true);
    for (size_t x = 0; x < 16; ++x) {
        size_t in_column = 0;
        for (size_t y = 0; y < 16; ++y) {
            in_column += fb.get_pixel(x, y) ? 1 : 0;
        }
        TEST_ASSERT_EQUAL(1, in_column);
    }

    // Windows and translated canvases at the limits clip without wrapping.
    fb.clear();
    canvas c(fb);
    c.window(INT_MIN, INT_MIN, 4, 4).fill(true);
    TEST_ASSERT_EQUAL(0, c.window(INT_MAX, 0, 4, 4).width());
    c.window(INT_MAX, INT_MAX, SIZE_MAX, SIZE_MAX).fill(true);
    canvas far(fb, INT_MIN, INT_MIN, 16, 16); // the surface lies wholly outside the space
    far.fill(true);
    far.draw_line(0, 0, 15, 15, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
    // A window's local coordinates end before INT_MAX. A window that spans
    // SIZE_MAX from INT_MIN would hold the canvas at local columns and rows
    // from 2^31, so it holds none of the canvas. A window from INT_MIN + 16
    // holds all but the last column and row.
    c.window(INT_MIN, INT_MIN, SIZE_MAX, SIZE_MAX).fill(true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
    c.window(INT_MIN + 16, INT_MIN + 16, SIZE_MAX, SIZE_MAX).fill(true);
    TEST_ASSERT_EQUAL(15 * 15, count_set_pixels(fb));
    TEST_ASSERT_FALSE(fb.get_pixel(15, 0));
    c.window(INT_MIN + 17, INT_MIN + 17, SIZE_MAX, SIZE_MAX).fill(true);
    TEST_ASSERT_EQUAL(16 * 16, count_set_pixels(fb));
}

TEST_CASE("gfx coordinate spaces reaching INT_MAX stop short of it", "[idfxx][gfx]") {
    auto fb = make_fb(12, 8);

    // The surface's columns 0-1 are at columns INT_MAX - 2 and INT_MAX - 1.
    // The rest are at INT_MAX and beyond, outside the space. A rectangle
    // whose right edge lies past INT_MAX draws only its top and bottom
    // edges' visible part, not a right edge clamped to column INT_MAX.
    canvas c(fb, INT_MAX - 2, 0, 0xffffffff, 4);
    c.draw_rect(0, 0, 0x80000005, 4, true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 0}, {1, 0}, {0, 3}, {1, 3}}));
    TEST_ASSERT_EQUAL(INT_MAX, c.width());

    // A window holding the surface at the same local columns.
    fb.clear();
    auto w = canvas(fb).window(INT_MIN + 3, 0, 0xffffffff, 4);
    w.draw_rect(0, 0, 0x80000005, 4, true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 0}, {1, 0}, {0, 3}, {1, 3}}));
    TEST_ASSERT_EQUAL(INT_MAX, w.width());

    // A right-aligned line of 'j' (ink one column left of the pen) whose pen
    // lands at INT_MAX inks column INT_MAX - 1. A line whose pen lands at
    // INT_MAX + 1 inks nothing.
    fb.clear();
    c.draw_text_box(prop_font, INT_MAX - 6, 0, 8, "j", true, text_align::right);
    TEST_ASSERT_TRUE(only_pixels(fb, {{1, 2}}));
    fb.clear();
    c.draw_text_box(prop_font, INT_MAX - 5, 0, 8, "j", true, text_align::right);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));

    // Text box lines starting past INT_MAX draw nothing, rather than
    // overprinting at row INT_MAX. Line 1 shows its glyph rows 0-1 on
    // surface rows 0-1 ('A' row 1 is inked at columns 1-2). Line 2 starts at
    // INT_MAX + 6.
    fb.clear();
    canvas tc(fb, 0, INT_MAX - 2, 12, 0xffffffff);
    TEST_ASSERT_EQUAL(
        3 * spleen_5x8.height, tc.draw_text_box(spleen_5x8, 0, INT_MAX - 10, 5, "A A A", true)
    );
    TEST_ASSERT_TRUE(only_pixels(fb, {{1, 1}, {2, 1}}));
    TEST_ASSERT_EQUAL(INT_MAX, tc.height());

    // A line of U+00E9 (ink one row above the line top) starting at row
    // INT_MAX inks row INT_MAX - 1. A line starting at INT_MAX + 1 inks nothing.
    fb.clear();
    tc.draw_text_box(prop_font, 0, INT_MAX - 6, 8, "A\né", true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 1}}));
    fb.clear();
    tc.draw_text_box(prop_font, 0, INT_MAX - 5, 8, "A\né", true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

namespace {

// A 9-column, one-row font: 'A' is inked only at column 0, 'B' only at
// column 7.
constexpr uint8_t wide_cell_bitmap[] = {0x80, 0x00, 0x01, 0x00};
constexpr mono_font wide_cell_font{
    .width = 9,
    .height = 1,
    .first_char = 'A',
    .last_char = 'B',
    .bitmap = wide_cell_bitmap,
};

} // namespace

TEST_CASE("gfx scaled mono text near the int limits draws the right glyph columns",
          "[idfxx][gfx]") {
    auto fb = make_fb(12, 8);
    canvas c(fb);

    // Glyph columns are 2^29 pixels wide from INT_MIN. This window would
    // hold the surface at local columns from 2^31 (glyph column 8), past
    // INT_MAX, so it holds none of it and nothing draws.
    auto past = c.window(INT_MIN, 0, 0xffffffff, 4);
    past.draw_text(wide_cell_font, INT_MIN, 0, "A", true, 1u << 29);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));

    // The surface at local columns INT_MAX - 12 to INT_MAX - 1, 2^32 - 13
    // to 2^32 - 1 pixels from INT_MIN: all in glyph column 7.
    auto w = c.window(INT_MIN + 13, 0, 0xffffffff, 4);
    TEST_ASSERT_EQUAL(INT_MAX, w.width());
    w.draw_text(wide_cell_font, INT_MIN, 0, "A", true, 1u << 29);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
    w.draw_text(wide_cell_font, INT_MIN, 0, "B", true, 1u << 29);
    TEST_ASSERT_EQUAL(12 * 4, count_set_pixels(fb));
}

TEST_CASE("gfx canvas window may start off its parent canvas", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    canvas c(fb);

    // Its local origin stays at (-4, -2) on the canvas: local columns 0-3
    // and rows 0-1 clip, and the reported size is the requested one.
    auto w = c.window(-4, -2, 8, 6);
    TEST_ASSERT_EQUAL(8, w.width());
    TEST_ASSERT_EQUAL(6, w.height());
    w.set_pixel(3, 1, true); // canvas (-1, -1)
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
    w.set_pixel(4, 2, true); // canvas (0, 0)
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0));
    w.fill(true);
    TEST_ASSERT_EQUAL(4 * 4, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(3, 3));

    // Drawing in a window off the canvas's corner matches the same content
    // drawn on the canvas, shifted.
    auto shifted = make_fb(16, 16);
    canvas sc(shifted);
    fb.clear();
    auto corner = c.window(-4, -2, 20, 18);
    corner.draw_text(spleen_5x8, 1, 0, "AB");
    corner.draw_line(0, 0, 7, 5, true);
    sc.draw_text(spleen_5x8, 1 - 4, 0 - 2, "AB");
    sc.draw_line(-4, -2, 3, 3, true);
    TEST_ASSERT_TRUE(count_set_pixels(fb) > 0);
    TEST_ASSERT_TRUE(same_pixels(fb, shifted));

    // A window reaching past a window's left and top stays inside it.
    fb.fill(true);
    auto parent = c.window(4, 4, 8, 8);
    parent.window(-6, -6, 20, 20).clear();
    TEST_ASSERT_EQUAL(16 * 16 - 8 * 8, count_set_pixels(fb));
    TEST_ASSERT_FALSE(fb.get_pixel(4, 4));
    TEST_ASSERT_FALSE(fb.get_pixel(11, 11));
    TEST_ASSERT_TRUE(fb.get_pixel(3, 4));
    TEST_ASSERT_TRUE(fb.get_pixel(12, 11));
}

TEST_CASE("gfx canvas with a negative origin clips the surface outside its space",
          "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    fb.fill(true);
    // The surface's (0, 0) sits at (-4, -4): its columns and rows 0-3 lie
    // outside the 16x16 space.
    canvas c(fb, -4, -4, 16, 16);
    c.clear();
    TEST_ASSERT_EQUAL(16 * 16 - 12 * 12, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(3, 15));
    TEST_ASSERT_FALSE(fb.get_pixel(4, 4));

    // A window spanning the whole surface still clips to the space, so its
    // clear and fill must not use the surface's whole-buffer clear and fill.
    fb.fill(true);
    auto w = c.window(-4, -4, 20, 20);
    w.clear();
    TEST_ASSERT_EQUAL(16 * 16 - 12 * 12, count_set_pixels(fb));
    fb.clear();
    w.fill(true);
    TEST_ASSERT_EQUAL(12 * 12, count_set_pixels(fb));
    fb.clear();
    w.set_pixel(0, 0, true); // surface (0, 0), outside the space
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

// =============================================================================
// Runtime tests: ellipses, circles and rounded rectangles
// =============================================================================

namespace {

// A shape of the round family, for the reference tests: the ellipse
// inscribed in a w x h box (r < 0), or a w x h rectangle with corners of
// radius r.
struct round_shape {
    long w;
    long h;
    long r;
};

// True when the center of pixel (i, j) of a w x h box lies inside or on
// the ellipse inscribed in the box.
bool in_ellipse(long w, long h, long i, long j) {
    if (i < 0 || j < 0 || i >= w || j >= h) {
        return false;
    }
    const int64_t u = 2 * i + 1 - w;
    const int64_t v = 2 * j + 1 - h;
    return u * u * h * h + v * v * w * w <= int64_t{w} * w * h * h;
}

// True when pixel (i, j) of the shape's box is inside the shape, straight
// from the documented contract.
bool in_round_shape(const round_shape& s, long i, long j) {
    if (s.r < 0) {
        return in_ellipse(s.w, s.h, i, j);
    }
    if (i < 0 || j < 0 || i >= s.w || j >= s.h) {
        return false;
    }
    const long r = std::min(s.r, std::min(s.w, s.h) / 2);
    // Corner pixels map onto the ellipse in the 2r x 2r box.
    const long ci = i < r ? i : (i >= s.w - r ? i - (s.w - 2 * r) : -1);
    const long cj = j < r ? j : (j >= s.h - r ? j - (s.h - 2 * r) : -1);
    return ci < 0 || cj < 0 || in_ellipse(2 * r, 2 * r, ci, cj);
}

// True when fill holds exactly the shape's pixels with its box at (ox, oy),
// and outline exactly those of them with a horizontal or vertical neighbor
// outside the shape.
bool matches_round_shape(
    const mono_framebuffer& fill,
    const mono_framebuffer& outline,
    int ox,
    int oy,
    const round_shape& s
) {
    for (long y = 0; y < static_cast<long>(fill.height()); ++y) {
        for (long x = 0; x < static_cast<long>(fill.width()); ++x) {
            const long i = x - ox;
            const long j = y - oy;
            const bool inside = in_round_shape(s, i, j);
            const bool boundary = inside &&
                (!in_round_shape(s, i - 1, j) || !in_round_shape(s, i + 1, j) ||
                 !in_round_shape(s, i, j - 1) || !in_round_shape(s, i, j + 1));
            if (fill.get_pixel(x, y) != inside || outline.get_pixel(x, y) != boundary) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("gfx ellipses fill the pixels centered inside and outline the fill's boundary",
          "[idfxx][gfx]") {
    // Even and odd sizes, inside the surface and clipped off every edge.
    auto fill = make_fb(16, 16);
    auto outline = make_fb(16, 16);
    for (size_t w = 0; w <= 13; ++w) {
        for (size_t h = 0; h <= 13; ++h) {
            for (const int ox : {-7, 3, 10}) {
                for (const int oy : {-6, 2, 9}) {
                    fill.clear();
                    outline.clear();
                    fill_ellipse(fill, ox, oy, w, h, true);
                    draw_ellipse(outline, ox, oy, w, h, true);
                    TEST_ASSERT_TRUE(matches_round_shape(
                        fill, outline, ox, oy, {static_cast<long>(w), static_cast<long>(h), -1}
                    ));
                }
            }
        }
    }
}

TEST_CASE("gfx rounded rectangles fill and outline ellipse-quadrant corners", "[idfxx][gfx]") {
    auto fill = make_fb(16, 16);
    auto outline = make_fb(16, 16);
    for (size_t w = 0; w <= 13; ++w) {
        for (size_t h = 0; h <= 13; h += (h < 4 ? 1 : 3)) {
            for (size_t r = 0; r <= 7; ++r) {
                for (const int o : {-5, 3, 9}) {
                    fill.clear();
                    outline.clear();
                    fill_round_rect(fill, o, o - 1, w, h, r, true);
                    draw_round_rect(outline, o, o - 1, w, h, r, true);
                    TEST_ASSERT_TRUE(matches_round_shape(
                        fill,
                        outline,
                        o,
                        o - 1,
                        {static_cast<long>(w), static_cast<long>(h), static_cast<long>(r)}
                    ));
                }
            }
        }
    }
}

TEST_CASE("gfx ellipses are symmetric and transpose with their box", "[idfxx][gfx]") {
    auto a = make_fb(16, 16);
    auto t = make_fb(16, 16);
    for (size_t w = 1; w <= 12; ++w) {
        for (size_t h = 1; h <= 12; ++h) {
            a.clear();
            t.clear();
            fill_ellipse(a, 0, 0, w, h, true);
            fill_ellipse(t, 0, 0, h, w, true);
            bool symmetric = true;
            for (size_t y = 0; y < h; ++y) {
                for (size_t x = 0; x < w; ++x) {
                    const bool p = a.get_pixel(x, y);
                    symmetric = symmetric && p == a.get_pixel(w - 1 - x, y) &&
                        p == a.get_pixel(x, h - 1 - y) && p == t.get_pixel(y, x);
                }
            }
            TEST_ASSERT_TRUE(symmetric);
        }
    }
}

TEST_CASE("gfx circles are ellipses in a 2r + 1 box", "[idfxx][gfx]") {
    auto circle = make_fb(24, 24);
    auto ellipse = make_fb(24, 24);
    for (size_t r = 0; r <= 7; ++r) {
        const int corner = static_cast<int>(r);
        circle.clear();
        ellipse.clear();
        fill_circle(circle, 9, 10, r, true);
        fill_ellipse(ellipse, 9 - corner, 10 - corner, 2 * r + 1, 2 * r + 1, true);
        TEST_ASSERT_TRUE(same_pixels(circle, ellipse));

        circle.clear();
        ellipse.clear();
        draw_circle(circle, 9, 10, r, true);
        draw_ellipse(ellipse, 9 - corner, 10 - corner, 2 * r + 1, 2 * r + 1, true);
        TEST_ASSERT_TRUE(same_pixels(circle, ellipse));
    }

    // The pixels centered within r + 0.5 of the center pixel's center:
    // radius 0 is the center pixel, radius 1 the 3 x 3 square around it,
    // and radius 2 that square plus two pixels along each axis.
    auto fb = make_fb(8, 8);
    fill_circle(fb, 3, 3, 0, true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{3, 3}}));
    fb.clear();
    draw_circle(fb, 3, 3, 0, true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{3, 3}}));
    fb.clear();
    fill_circle(fb, 3, 3, 1, true);
    TEST_ASSERT_EQUAL(9, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(2, 2) && fb.get_pixel(4, 4));
    fb.clear();
    draw_circle(fb, 3, 3, 1, true);
    TEST_ASSERT_EQUAL(8, count_set_pixels(fb));
    TEST_ASSERT_FALSE(fb.get_pixel(3, 3));
    fb.clear();
    fill_circle(fb, 3, 3, 2, true);
    TEST_ASSERT_EQUAL(21, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(1, 2) && fb.get_pixel(5, 4) && fb.get_pixel(3, 1));
    TEST_ASSERT_FALSE(fb.get_pixel(1, 1) || fb.get_pixel(5, 5));
}

TEST_CASE("gfx rounded rectangles reduce to ellipses and rectangles", "[idfxx][gfx]") {
    auto a = make_fb(16, 16);
    auto b = make_fb(16, 16);
    // A 2r x 2r rounded rectangle of radius r is the ellipse in that box.
    for (size_t r = 1; r <= 7; ++r) {
        a.clear();
        b.clear();
        fill_round_rect(a, 1, 2, 2 * r, 2 * r, r, true);
        fill_ellipse(b, 1, 2, 2 * r, 2 * r, true);
        TEST_ASSERT_TRUE(same_pixels(a, b));
        a.clear();
        b.clear();
        draw_round_rect(a, 1, 2, 2 * r, 2 * r, r, true);
        draw_ellipse(b, 1, 2, 2 * r, 2 * r, true);
        TEST_ASSERT_TRUE(same_pixels(a, b));
    }
    // Radius 0 is the plain rectangle, and so is radius 1 (the 2 x 2
    // corner ellipse covers its whole box).
    for (const size_t r : {0, 1}) {
        a.clear();
        b.clear();
        fill_round_rect(a, 2, 3, 9, 6, r, true);
        fill_rect(b, 2, 3, 9, 6, true);
        TEST_ASSERT_TRUE(same_pixels(a, b));
        a.clear();
        b.clear();
        draw_round_rect(a, 2, 3, 9, 6, r, true);
        draw_rect(b, 2, 3, 9, 6, true);
        TEST_ASSERT_TRUE(same_pixels(a, b));
    }
    // Radii 2 and 3 each trim one pixel from each corner.
    for (const size_t r : {2, 3}) {
        a.clear();
        fill_round_rect(a, 2, 3, 9, 6, r, true);
        TEST_ASSERT_EQUAL(9 * 6 - 4, count_set_pixels(a));
        TEST_ASSERT_FALSE(
            a.get_pixel(2, 3) || a.get_pixel(10, 3) || a.get_pixel(2, 8) || a.get_pixel(10, 8)
        );
    }
    // The radius is clamped to half the shorter side.
    a.clear();
    b.clear();
    fill_round_rect(a, 2, 3, 9, 6, SIZE_MAX, true);
    fill_round_rect(b, 2, 3, 9, 6, 3, true);
    TEST_ASSERT_TRUE(same_pixels(a, b));
}

TEST_CASE("gfx thin even ellipses leave rows without a pixel center inside empty",
          "[idfxx][gfx]") {
    // In a 2 x 8 box, no pixel center of the top and bottom rows lies inside
    // the ellipse: its ink spans rows 1-6 of the box.
    auto fb = make_fb(4, 16);
    fill_ellipse(fb, 1, 1, 2, 8, true);
    TEST_ASSERT_EQUAL(0, count_set_rows(fb, 0, 2));
    TEST_ASSERT_EQUAL(0, count_set_rows(fb, 8, 16));
    TEST_ASSERT_EQUAL(12, count_set_pixels(fb));
}

TEST_CASE("gfx round shapes with a zero size draw nothing", "[idfxx][gfx]") {
    auto fb = make_fb(8, 8);
    fill_ellipse(fb, 2, 2, 0, 4, true);
    fill_ellipse(fb, 2, 2, 4, 0, true);
    draw_ellipse(fb, 2, 2, 0, 0, true);
    fill_round_rect(fb, 2, 2, 0, 4, 1, true);
    draw_round_rect(fb, 2, 2, 4, 0, 1, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

TEST_CASE("gfx round shapes at the int and size limits clip exactly", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);

    // A box from INT_MIN spanning SIZE_MAX is centered near the origin, so
    // the whole surface lies inside its ellipse and rounded rectangle, and
    // their outlines lie far off the surface.
    fill_ellipse(fb, INT_MIN, INT_MIN, SIZE_MAX, SIZE_MAX, true);
    TEST_ASSERT_EQUAL(16 * 16, count_set_pixels(fb));
    fb.clear();
    fill_round_rect(fb, INT_MIN, INT_MIN, SIZE_MAX, SIZE_MAX, SIZE_MAX, true);
    TEST_ASSERT_EQUAL(16 * 16, count_set_pixels(fb));
    fb.clear();
    draw_ellipse(fb, INT_MIN, INT_MIN, SIZE_MAX, SIZE_MAX, true);
    draw_round_rect(fb, INT_MIN, INT_MIN, SIZE_MAX, SIZE_MAX, 40, true);
    fill_ellipse(fb, INT_MAX, INT_MAX, SIZE_MAX, SIZE_MAX, true);
    fill_circle(fb, INT_MIN, INT_MIN, 100, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));

    // A circle of radius 2^30 + 3 centered 2^30 left of the surface reaches
    // columns 0-3 of every visible row. Its terms near 2^124 need the full
    // 128-bit test.
    fill_circle(fb, -(1 << 30), 4, (size_t{1} << 30) + 3, true);
    TEST_ASSERT_EQUAL(4 * 16, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(3, 15) && !fb.get_pixel(4, 0));
    fb.clear();
    draw_circle(fb, -(1 << 30), 4, (size_t{1} << 30) + 3, true);
    TEST_ASSERT_EQUAL(16, count_set_pixels(fb)); // the outline: column 3
    TEST_ASSERT_TRUE(fb.get_pixel(3, 0) && fb.get_pixel(3, 15));

    // A radius past INT_MAX is treated as INT_MAX: centered at INT_MIN + 1,
    // the circle reaches column 0 alone.
    fb.clear();
    fill_circle(fb, INT_MIN + 1, 4, SIZE_MAX, true);
    TEST_ASSERT_EQUAL(16, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(0, 0) && fb.get_pixel(0, 15));
}

// =============================================================================
// Runtime tests: triangles
// =============================================================================

namespace {

// True when the center of pixel (px, py) lies inside or on the triangle
// with vertices (v[0], v[1]), (v[2], v[3]) and (v[4], v[5]). A degenerate
// triangle holds its segment.
bool in_triangle(const int (&v)[6], long px, long py) {
    const auto edge = [&](int a, int b) {
        return int64_t{v[b] - v[a]} * (py - v[a + 1]) - int64_t{v[b + 1] - v[a + 1]} * (px - v[a]);
    };
    const int64_t e0 = edge(0, 2);
    const int64_t e1 = edge(2, 4);
    const int64_t e2 = edge(4, 0);
    const int64_t area =
        int64_t{v[2] - v[0]} * (v[5] - v[1]) - int64_t{v[3] - v[1]} * (v[4] - v[0]);
    if (area != 0) {
        return (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
    }
    const auto on_segment = [&](int a, int b, int64_t e) {
        return e == 0 && px >= std::min(v[a], v[b]) && px <= std::max(v[a], v[b]) &&
            py >= std::min(v[a + 1], v[b + 1]) && py <= std::max(v[a + 1], v[b + 1]);
    };
    return on_segment(0, 2, e0) || on_segment(2, 4, e1) || on_segment(4, 0, e2);
}

// Pixel (x, y) of fb, false off its edges.
bool pixel_at(const mono_framebuffer& fb, long x, long y) {
    return x >= 0 && y >= 0 && fb.get_pixel(x, y);
}

// True when fb holds fill_triangle's documented result for v: the pixels
// centered inside the triangle or on an edge, together with the pixels of
// outline, the triangle drawn by draw_triangle. With `rows`, also true only
// when every row of that oracle is one run (checked on the oracle itself).
bool fills_union(
    const mono_framebuffer& fb,
    const mono_framebuffer& outline,
    const int (&v)[6],
    bool rows
) {
    for (long y = 0; y < static_cast<long>(fb.height()); ++y) {
        long runs = 0;
        bool previous = false;
        for (long x = 0; x < static_cast<long>(fb.width()); ++x) {
            const bool expected = in_triangle(v, x, y) || outline.get_pixel(x, y);
            if (fb.get_pixel(x, y) != expected) {
                return false;
            }
            runs += expected && !previous ? 1 : 0;
            previous = expected;
        }
        if (rows && runs > 1) {
            return false;
        }
    }
    return true;
}

// True when outline's pixels are all in fill, and every fill pixel with a
// horizontal or vertical neighbor outside the fill is an outline pixel.
bool outline_bounds_fill(const mono_framebuffer& fill, const mono_framebuffer& outline) {
    for (long y = 0; y < static_cast<long>(fill.height()); ++y) {
        for (long x = 0; x < static_cast<long>(fill.width()); ++x) {
            if (outline.get_pixel(x, y) && !fill.get_pixel(x, y)) {
                return false;
            }
            const bool edge = !pixel_at(fill, x - 1, y) || !pixel_at(fill, x + 1, y) ||
                !pixel_at(fill, x, y - 1) || !pixel_at(fill, x, y + 1);
            if (fill.get_pixel(x, y) && edge && !outline.get_pixel(x, y)) {
                return false;
            }
        }
    }
    return true;
}

// True when fb's set pixels (at most 32 x 32 of them) are 8-connected.
bool eight_connected(const mono_framebuffer& fb) {
    const long w = static_cast<long>(fb.width());
    const long h = static_cast<long>(fb.height());
    std::array<bool, 32 * 32> seen{};
    std::array<int16_t, 32 * 32> stack{};
    size_t depth = 0;
    size_t total = 0;
    for (long i = 0; i < w * h; ++i) {
        if (fb.get_pixel(i % w, i / w)) {
            if (total++ == 0) {
                stack[depth++] = static_cast<int16_t>(i);
                seen[i] = true;
            }
        }
    }
    size_t reached = 0;
    while (depth > 0) {
        const long i = stack[--depth];
        ++reached;
        for (long dy = -1; dy <= 1; ++dy) {
            for (long dx = -1; dx <= 1; ++dx) {
                const long x = i % w + dx;
                const long y = i / w + dy;
                if (x >= 0 && y >= 0 && x < w && y < h && !seen[y * w + x] && fb.get_pixel(x, y)) {
                    seen[y * w + x] = true;
                    stack[depth++] = static_cast<int16_t>(y * w + x);
                }
            }
        }
    }
    return reached == total;
}

} // namespace

TEST_CASE("gfx fill_triangle fills the pixels centered inside and those draw_triangle draws",
          "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    auto outline = make_fb(16, 16);
    // Pseudo-random triangles over and around the surface, clipped at every
    // edge, every seventh one degenerate.
    uint32_t seed = 1;
    const auto next = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<int>((seed >> 16) % 24) - 4;
    };
    for (int n = 0; n < 1000; ++n) {
        int v[6];
        for (int& c : v) {
            c = next();
        }
        if (n % 7 == 0) {
            v[4] = v[0] + 2 * (v[2] - v[0]);
            v[5] = v[1] + 2 * (v[3] - v[1]);
        }
        fb.clear();
        outline.clear();
        fill_triangle(fb, v[0], v[1], v[2], v[3], v[4], v[5], true);
        draw_triangle(outline, v[0], v[1], v[2], v[3], v[4], v[5], true);
        TEST_ASSERT_TRUE(fills_union(fb, outline, v, false));
    }
}

TEST_CASE("gfx fill_triangle covers its outline, which bounds the fill", "[idfxx][gfx]") {
    // Every triangle with its vertices in a 5x5 grid (degenerate, collinear,
    // flat-topped and flat-bottomed ones included), drawn at least a pixel in
    // from the surface's edges so that no neighbor is clipped. Each row of
    // the fill is one run, the outline lies in the fill, and the fill's
    // boundary is the outline. The outline is each edge drawn as a line from
    // its upper endpoint (the grid points a <= b <= c are in that order).
    // Every order of the same vertices fills and outlines exactly the same
    // pixels.
    auto fb = make_fb(8, 8);
    auto outline = make_fb(8, 8);
    auto lines = make_fb(8, 8);
    auto other_fb = make_fb(8, 8);
    auto other_outline = make_fb(8, 8);
    const int orders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    size_t count = 0;
    for (int a = 0; a < 25; ++a) {
        for (int b = a; b < 25; ++b) {
            for (int c = b; c < 25; ++c) {
                const int v[6] = {1 + a % 5, 1 + a / 5, 1 + b % 5, 1 + b / 5, 1 + c % 5, 1 + c / 5};
                fb.clear();
                outline.clear();
                fill_triangle(fb, v[0], v[1], v[2], v[3], v[4], v[5], true);
                draw_triangle(outline, v[0], v[1], v[2], v[3], v[4], v[5], true);
                const bool ok = fills_union(fb, outline, v, true) &&
                    outline_bounds_fill(fb, outline) && eight_connected(fb);
                TEST_ASSERT_TRUE(ok);
                lines.clear();
                draw_line(lines, v[0], v[1], v[2], v[3], true);
                draw_line(lines, v[2], v[3], v[4], v[5], true);
                draw_line(lines, v[0], v[1], v[4], v[5], true);
                TEST_ASSERT_TRUE(same_pixels(lines, outline));
                for (const auto& o : orders) {
                    const int* p = &v[2 * o[0]];
                    const int* q = &v[2 * o[1]];
                    const int* r = &v[2 * o[2]];
                    other_fb.clear();
                    other_outline.clear();
                    fill_triangle(other_fb, p[0], p[1], q[0], q[1], r[0], r[1], true);
                    draw_triangle(other_outline, p[0], p[1], q[0], q[1], r[0], r[1], true);
                    const bool same =
                        same_pixels(fb, other_fb) && same_pixels(outline, other_outline);
                    TEST_ASSERT_TRUE(same);
                }
                ++count;
            }
        }
    }
    TEST_ASSERT_EQUAL(25 * 26 * 27 / 6, count); // every multiset of three grid points
}

TEST_CASE("gfx fill_triangle of slivers and sharp tips is connected and bounded by its outline",
          "[idfxx][gfx]") {
    // Long thin triangles across a 32x32 surface: the third vertex sits up
    // to a pixel off a point along the first two's segment, or just past an
    // end of it (a sharp tip).
    auto fb = make_fb(32, 32);
    auto outline = make_fb(32, 32);
    uint32_t seed = 7;
    const auto next = [&seed](int range) {
        seed = seed * 1103515245u + 12345u;
        return static_cast<int>((seed >> 16) % range);
    };
    for (int n = 0; n < 1500; ++n) {
        int v[6];
        for (int i = 0; i < 4; ++i) {
            v[i] = 1 + next(30);
        }
        const int t = next(7) - 1; // -1/4 to 5/4 of the way along
        v[4] = std::clamp(v[0] + (v[2] - v[0]) * t / 4 + next(3) - 1, 1, 30);
        v[5] = std::clamp(v[1] + (v[3] - v[1]) * t / 4 + next(3) - 1, 1, 30);
        fb.clear();
        outline.clear();
        fill_triangle(fb, v[0], v[1], v[2], v[3], v[4], v[5], true);
        draw_triangle(outline, v[0], v[1], v[2], v[3], v[4], v[5], true);
        const bool ok = fills_union(fb, outline, v, true) && outline_bounds_fill(fb, outline) &&
            eight_connected(fb);
        TEST_ASSERT_TRUE(ok);
    }
}

TEST_CASE("gfx fill_triangle includes its vertices and edges in any vertex order",
          "[idfxx][gfx]") {
    auto fb = make_fb(8, 8);
    auto first = make_fb(8, 8);
    fill_triangle(first, 1, 1, 3, 1, 1, 3, true);
    TEST_ASSERT_TRUE(only_pixels(first, {{1, 1}, {2, 1}, {3, 1}, {1, 2}, {2, 2}, {1, 3}}));
    const int v[3][2] = {{1, 1}, {3, 1}, {1, 3}};
    const int orders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (const auto& o : orders) {
        fb.clear();
        const auto& a = v[o[0]];
        const auto& b = v[o[1]];
        const auto& c = v[o[2]];
        fill_triangle(fb, a[0], a[1], b[0], b[1], c[0], c[1], true);
        TEST_ASSERT_TRUE(same_pixels(first, fb));
    }

    // A clipped triangle with an edge with half-pixel ties fills and
    // outlines the same pixels in every order.
    auto fill = make_fb(16, 16);
    auto outline = make_fb(16, 16);
    auto first_fill = make_fb(16, 16);
    auto first_outline = make_fb(16, 16);
    const int w[3][2] = {{-3, -7}, {14, 5}, {1, 1}};
    fill_triangle(first_fill, -3, -7, 14, 5, 1, 1, true);
    draw_triangle(first_outline, -3, -7, 14, 5, 1, 1, true);
    for (const auto& o : orders) {
        fill.clear();
        outline.clear();
        const auto& a = w[o[0]];
        const auto& b = w[o[1]];
        const auto& c = w[o[2]];
        fill_triangle(fill, a[0], a[1], b[0], b[1], c[0], c[1], true);
        draw_triangle(outline, a[0], a[1], b[0], b[1], c[0], c[1], true);
        TEST_ASSERT_TRUE(same_pixels(first_fill, fill) && same_pixels(first_outline, outline));
    }
}

TEST_CASE("gfx degenerate triangles fill the pixels draw_triangle draws", "[idfxx][gfx]") {
    auto fb = make_fb(8, 8);
    auto outline = make_fb(8, 8);
    fill_triangle(fb, 4, 4, 4, 4, 4, 4, true); // a point
    TEST_ASSERT_TRUE(only_pixels(fb, {{4, 4}}));
    fb.clear();
    fill_triangle(fb, 1, 2, 6, 2, 3, 2, true); // horizontal
    TEST_ASSERT_TRUE(only_pixels(fb, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}}));
    fb.clear();
    fill_triangle(fb, 0, 0, 4, 4, 2, 2, true); // diagonal
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}}));
    fb.clear();
    // Steep, with half-pixel ties: every edge is walked down from its upper
    // endpoint, so the long edge's pixels match the two short ones'.
    fill_triangle(fb, 0, 0, 1, 2, 2, 4, true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 0}, {1, 1}, {1, 2}, {2, 3}, {2, 4}}));
    fb.clear();
    fill_triangle(fb, 2, 4, 1, 2, 0, 0, true);
    TEST_ASSERT_TRUE(only_pixels(fb, {{0, 0}, {1, 1}, {1, 2}, {2, 3}, {2, 4}}));

    // Each fills exactly its outline.
    const int degenerate[][6] = {
        {4, 4, 4, 4, 4, 4},
        {1, 2, 6, 2, 3, 2},
        {0, 0, 4, 4, 2, 2},
        {0, 0, 1, 2, 2, 4},
        {7, 1, 1, 4, 5, 2},
    };
    for (const auto& v : degenerate) {
        fb.clear();
        outline.clear();
        fill_triangle(fb, v[0], v[1], v[2], v[3], v[4], v[5], true);
        draw_triangle(outline, v[0], v[1], v[2], v[3], v[4], v[5], true);
        TEST_ASSERT_TRUE(same_pixels(outline, fb));
    }
}

TEST_CASE("gfx triangles sharing edges tile without gaps", "[idfxx][gfx]") {
    // A rectangle split along a diagonal, and a hexagon fanned around an
    // inner point, fill exactly the pixels of the whole.
    auto fb = make_fb(16, 16);
    fill_triangle(fb, 1, 1, 13, 1, 13, 9, true);
    fill_triangle(fb, 1, 1, 13, 9, 1, 9, true);
    auto whole = make_fb(16, 16);
    fill_rect(whole, 1, 1, 13, 9, true);
    TEST_ASSERT_TRUE(same_pixels(whole, fb));

    fb.clear();
    const int hex[6][2] = {{7, 0}, {14, 4}, {14, 11}, {7, 15}, {0, 11}, {0, 4}};
    for (int i = 0; i < 6; ++i) {
        const auto& a = hex[i];
        const auto& b = hex[(i + 1) % 6];
        fill_triangle(fb, 6, 8, a[0], a[1], b[0], b[1], true);
    }
    whole.clear();
    fill_triangle(whole, 7, 0, 14, 4, 14, 11, true);
    fill_triangle(whole, 7, 0, 14, 11, 7, 15, true);
    fill_triangle(whole, 7, 0, 7, 15, 0, 11, true);
    fill_triangle(whole, 7, 0, 0, 11, 0, 4, true);
    TEST_ASSERT_TRUE(same_pixels(whole, fb));
}

TEST_CASE("gfx draw_triangle draws each edge as a line from its upper endpoint",
          "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    auto lines = make_fb(16, 16);
    draw_triangle(fb, -3, 2, 14, 5, 6, 19, true);
    draw_line(lines, -3, 2, 14, 5, true);
    draw_line(lines, 14, 5, 6, 19, true);
    draw_line(lines, -3, 2, 6, 19, true);
    TEST_ASSERT_TRUE(same_pixels(lines, fb));

    // The edge between (1, 1) and (-3, -7) has half-pixel ties, which a line
    // drawn up from (1, 1) rounds differently: the triangle draws it down.
    fb.clear();
    lines.clear();
    draw_triangle(fb, -3, -7, 14, 5, 1, 1, true);
    draw_line(lines, -3, -7, 14, 5, true);
    draw_line(lines, 1, 1, 14, 5, true);
    draw_line(lines, -3, -7, 1, 1, true);
    TEST_ASSERT_TRUE(same_pixels(lines, fb));
    lines.clear();
    draw_line(lines, -3, -7, 14, 5, true);
    draw_line(lines, 14, 5, 1, 1, true);
    draw_line(lines, 1, 1, -3, -7, true);
    TEST_ASSERT_FALSE(same_pixels(lines, fb));
}

TEST_CASE("gfx triangles at the int limits clip exactly", "[idfxx][gfx]") {
    auto fb = make_fb(16, 16);
    // Far vertices with the edge x + y = 15 crossing the surface fill the
    // pixels with x + y <= 15.
    fill_triangle(fb, INT_MIN, INT_MIN, INT_MAX, 15 - INT_MAX, 15 - INT_MAX, INT_MAX, true);
    TEST_ASSERT_EQUAL(16 * 17 / 2, count_set_pixels(fb));
    TEST_ASSERT_TRUE(fb.get_pixel(15, 0) && fb.get_pixel(0, 15) && fb.get_pixel(7, 8));
    TEST_ASSERT_FALSE(fb.get_pixel(8, 8));

    fb.clear();
    fill_triangle(fb, INT_MIN, INT_MIN, INT_MAX, INT_MIN, INT_MIN, INT_MAX, true); // x + y <= -1
    fill_triangle(fb, INT_MAX, INT_MAX, INT_MAX, INT_MAX, INT_MAX, INT_MAX, true);
    draw_triangle(fb, INT_MIN, INT_MIN, INT_MAX, INT_MIN, INT_MIN, INT_MIN, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

// =============================================================================
// Runtime tests: bitmaps
// =============================================================================

namespace {

// A 10x3 image, two bytes per row, with padding bits set in both bit
// orders. MSB-first its rows are X......X.X, ..XXXX.... and X.........;
// LSB-first X......XXX, ..XXXX..XX and .......XXX.
constexpr uint8_t bitmap_bits[] = {0x81, 0x7F, 0x3C, 0x3F, 0x80, 0x3F};
constexpr mono_bitmap msb_image{bitmap_bits, 10, 3};
constexpr mono_bitmap lsb_image{bitmap_bits, 10, 3, bit_order::lsb_first};

static_assert(msb_image.bytes_per_row() == 2);
static_assert(mono_bitmap{{}, 8, 1}.bytes_per_row() == 1);
static_assert(mono_bitmap{{}, 0, 1}.bytes_per_row() == 0);
static_assert(mono_bitmap{{}, SIZE_MAX, 1}.bytes_per_row() == SIZE_MAX / 8 + 1);

// A bitmap over a constexpr table is itself a constant expression (the
// documented icon-table form), and refers to the whole table.
constexpr uint8_t check_bits[] = {0x01, 0x03, 0x86, 0xCC, 0x78, 0x30};
constexpr mono_bitmap check{check_bits, 8, 6};
static_assert(check.data.data() == check_bits && check.data.size() == 6);
static_assert(msb_image.data.size() == std::size(bitmap_bits));
static_assert(mono_bitmap{.width = 8, .height = 1}.data.empty());

// True when pixel (col, row) of a test image is set, decoded apart from gfx.
bool image_bit(const mono_bitmap& b, size_t col, size_t row) {
    const uint8_t byte = b.data[row * ((b.width + 7) / 8) + col / 8];
    return b.order == bit_order::msb_first ? (byte & (0x80u >> (col % 8))) != 0
                                           : (byte & (1u << (col % 8))) != 0;
}

// True when fb shows image b drawn at (ox, oy) over a background of
// `background`: set bits are true, and clear bits *paper (or the
// background, when paper is null).
bool shows_image(
    const mono_framebuffer& fb,
    const mono_bitmap& b,
    int ox,
    int oy,
    bool background,
    const bool* paper
) {
    for (long y = 0; y < static_cast<long>(fb.height()); ++y) {
        for (long x = 0; x < static_cast<long>(fb.width()); ++x) {
            const long col = x - ox;
            const long row = y - oy;
            bool expected = background;
            if (col >= 0 && row >= 0 && col < static_cast<long>(b.width) &&
                row < static_cast<long>(b.height)) {
                expected = image_bit(b, col, row) || (paper != nullptr ? *paper : background);
            }
            if (fb.get_pixel(x, y) != expected) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("bit_order to_string and formatter", "[idfxx][gfx]") {
    TEST_ASSERT_EQUAL_STRING("msb_first", idfxx::to_string(bit_order::msb_first).c_str());
    TEST_ASSERT_EQUAL_STRING("lsb_first", idfxx::to_string(bit_order::lsb_first).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(99)", idfxx::to_string(static_cast<bit_order>(99)).c_str());
#ifdef CONFIG_IDFXX_STD_FORMAT
    auto s = std::format("{}", bit_order::lsb_first);
    TEST_ASSERT_EQUAL_STRING("lsb_first", s.c_str());
#endif
}

TEST_CASE("gfx draw_bitmap decodes both bit orders and ignores row padding", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    draw_bitmap(fb, 2, 1, msb_image, true);
    TEST_ASSERT_TRUE(
        only_pixels(fb, {{2, 1}, {9, 1}, {11, 1}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {2, 3}})
    );
    fb.clear();
    draw_bitmap(fb, 2, 1, lsb_image, true);
    TEST_ASSERT_TRUE(only_pixels(
        fb,
        {{2, 1}, {9, 1}, {10, 1}, {11, 1},
         {4, 2}, {5, 2}, {6, 2}, {7, 2}, {10, 2}, {11, 2},
         {9, 3}, {10, 3}, {11, 3}}
    ));
}

TEST_CASE("gfx draw_bitmap is transparent, or opaque with a paper value", "[idfxx][gfx]") {
    const bool paper = false;
    auto fb = make_fb(16, 8);
    for (const mono_bitmap* b : {&msb_image, &lsb_image}) {
        // Transparent: clear bits leave the background, set or not.
        for (const bool background : {false, true}) {
            fb.fill(background);
            draw_bitmap(fb, 3, 2, *b, true);
            TEST_ASSERT_TRUE(shows_image(fb, *b, 3, 2, background, nullptr));
        }
        // Opaque: every pixel the image covers is replaced.
        fb.fill(true);
        draw_bitmap(fb, 3, 2, *b, true, false);
        TEST_ASSERT_TRUE(shows_image(fb, *b, 3, 2, true, &paper));
        // Inverse: ink false erases the image's set pixels from a filled area.
        fb.fill(true);
        draw_bitmap(fb, 3, 2, *b, false);
        for (size_t row = 0; row < b->height; ++row) {
            for (size_t col = 0; col < b->width; ++col) {
                TEST_ASSERT_EQUAL(!image_bit(*b, col, row), fb.get_pixel(3 + col, 2 + row));
            }
        }
    }
}

TEST_CASE("gfx draw_bitmap draws only the complete rows its data holds", "[idfxx][gfx]") {
    // The image's first two rows and half its third, then bytes with every
    // bit set: a read past the data would draw them.
    constexpr uint8_t bits[] = {0x81, 0x7F, 0x3C, 0x3F, 0x80, 0xFF, 0xFF, 0xFF, 0xFF};
    const mono_bitmap short_image{std::span(bits).first(5), 10, 4};
    const mono_bitmap two_rows{bitmap_bits, 10, 2};
    auto fb = make_fb(16, 8);
    auto expected = make_fb(16, 8);
    draw_bitmap(fb, 2, 1, short_image, true);
    draw_bitmap(expected, 2, 1, two_rows, true);
    TEST_ASSERT_TRUE(same_pixels(expected, fb));
    TEST_ASSERT_TRUE(shows_image(fb, two_rows, 2, 1, false, nullptr));

    // Opaque, through a canvas: only the two complete rows are written.
    counting_surface surface;
    canvas c(surface);
    c.draw_bitmap(-3, 2, short_image, 1, 2);
    TEST_ASSERT_EQUAL(7 * 2, surface.writes);
    for (size_t y = 0; y < 8; ++y) {
        for (size_t x = 0; x < 8; ++x) {
            const bool covered = x < 7 && (y == 2 || y == 3);
            const int value = !covered ? 0 : (image_bit(two_rows, x + 3, y - 2) ? 1 : 2);
            TEST_ASSERT_EQUAL(value, surface.pixels[y][x]);
        }
    }

    // No data, or less than one row, draws nothing.
    surface = {};
    draw_bitmap(surface, 0, 0, mono_bitmap{{}, 10, 4}, 1, 2);
    draw_bitmap(surface, 0, 0, mono_bitmap{std::span(bits).first(1), 10, 4}, 1, 2);
    c.draw_bitmap(0, 0, mono_bitmap{std::span(bits).first(1), 10, 4}, 1);
    TEST_ASSERT_EQUAL(0, surface.writes);
}

TEST_CASE("gfx draw_bitmap layers transparent images in different inks", "[idfxx][gfx]") {
    // A battery outline and its level, drawn at one origin: each ink lands
    // only on its own layer's set bits.
    constexpr uint8_t outline_bits[] = {0xF0, 0x90, 0xF0};
    constexpr uint8_t level_bits[] = {0x00, 0x60, 0x00};
    counting_surface surface;
    draw_bitmap(surface, 1, 2, mono_bitmap{outline_bits, 4, 3}, 1);
    draw_bitmap(surface, 1, 2, mono_bitmap{level_bits, 4, 3}, 2);
    TEST_ASSERT_EQUAL(12, surface.writes);
    for (size_t y = 0; y < 8; ++y) {
        for (size_t x = 0; x < 8; ++x) {
            const bool box = x >= 1 && x < 5 && y >= 2 && y < 5;
            const bool level = box && y == 3 && (x == 2 || x == 3);
            TEST_ASSERT_EQUAL(level ? 2 : (box ? 1 : 0), surface.pixels[y][x]);
        }
    }
}

TEST_CASE("gfx draw_bitmap clips off every edge", "[idfxx][gfx]") {
    const bool paper = false;
    auto fb = make_fb(8, 8);
    for (const int ox : {-9, -4, -1, 0, 3, 7, 8}) {
        for (const int oy : {-3, -1, 0, 6, 8}) {
            fb.clear();
            draw_bitmap(fb, ox, oy, msb_image, true);
            TEST_ASSERT_TRUE(shows_image(fb, msb_image, ox, oy, false, nullptr));
            fb.fill(true);
            draw_bitmap(fb, ox, oy, lsb_image, true, false);
            TEST_ASSERT_TRUE(shows_image(fb, lsb_image, ox, oy, true, &paper));
        }
    }
    fb.clear();
    draw_bitmap(fb, INT_MIN, INT_MIN, msb_image, true, true);
    draw_bitmap(fb, INT_MAX, 0, msb_image, true, true);
    draw_bitmap(fb, 0, INT_MAX, msb_image, true, true);
    draw_bitmap(fb, 0, 0, mono_bitmap{bitmap_bits, 0, 3}, true, true);
    draw_bitmap(fb, 0, 0, mono_bitmap{bitmap_bits, 10, 0}, true, true);
    TEST_ASSERT_EQUAL(0, count_set_pixels(fb));
}

// =============================================================================
// Runtime tests: shapes through canvases and bands
// =============================================================================

namespace {

constexpr uint8_t icon_bits[] = {0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C};
constexpr mono_bitmap icon{icon_bits, 8, 8};

// Draws every shape and bitmap primitive across a 32x32 frame, crossing
// band edges.
template<typename Canvas>
void draw_shape_scene(Canvas& c) {
    c.fill_ellipse(-3, 2, 15, 11, true);
    c.draw_ellipse(10, 5, 20, 14, true);
    c.fill_circle(24, 24, 6, true);
    c.draw_circle(8, 22, 9, true);
    c.fill_round_rect(14, 0, 12, 9, 4, true);
    c.draw_round_rect(2, 14, 26, 15, 5, true);
    c.fill_triangle(0, 31, 12, 18, 20, 30, true);
    c.draw_triangle(31, 0, 18, 15, 29, 20, true);
    c.draw_bitmap(12, 4, icon, false);
    c.draw_bitmap(26, 14, icon, true, false);
}

} // namespace

TEST_CASE("gfx canvas shape members draw like the free functions", "[idfxx][gfx]") {
    auto fb = make_fb(32, 32);
    canvas c(fb);
    draw_shape_scene(c);

    auto expected = make_fb(32, 32);
    fill_ellipse(expected, -3, 2, 15, 11, true);
    draw_ellipse(expected, 10, 5, 20, 14, true);
    fill_circle(expected, 24, 24, 6, true);
    draw_circle(expected, 8, 22, 9, true);
    fill_round_rect(expected, 14, 0, 12, 9, 4, true);
    draw_round_rect(expected, 2, 14, 26, 15, 5, true);
    fill_triangle(expected, 0, 31, 12, 18, 20, 30, true);
    draw_triangle(expected, 31, 0, 18, 15, 29, 20, true);
    draw_bitmap(expected, 12, 4, icon, false);
    draw_bitmap(expected, 26, 14, icon, true, false);
    TEST_ASSERT_TRUE(same_pixels(expected, fb));
}

TEST_CASE("gfx clipping: shapes render bands identical to a full frame", "[idfxx][gfx]") {
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { draw_shape_scene(c); }));
    TEST_ASSERT_TRUE(bands_match_frame([](auto& c) { draw_shape_scene(c); }, 5));
}

TEST_CASE("gfx clipping: shapes write each visible pixel once and nothing outside the band",
          "[idfxx][gfx]") {
    // The surface backs columns 0-7 of rows 16-23 of a 32x32 space.
    sized_counter<8, 8> surf;
    canvas band(surf, 0, 16, 32, 32);
    const auto set_pixels = [&surf] {
        size_t n = 0;
        for (size_t y = 0; y < 8; ++y) {
            for (size_t x = 0; x < 8; ++x) {
                n += surf.get_pixel(x, y) ? 1 : 0;
            }
        }
        return n;
    };

    // Shapes whose bounding boxes do not overlap the band make no set_pixel call.
    band.fill_ellipse(0, 0, 30, 15, true);
    band.draw_circle(10, 6, 5, true);
    band.fill_round_rect(0, 24, 8, 8, 2, true);
    band.fill_triangle(0, 0, 31, 0, 0, 15, true);
    band.draw_bitmap(0, 8, icon, true, false);
    band.draw_bitmap(8, 16, icon, true, false);
    TEST_ASSERT_EQUAL(0, surf.writes);

    // Shapes that cross the band write each band pixel that they cover once.
    band.fill_circle(4, 20, 6, true);
    TEST_ASSERT_TRUE(surf.writes > 0);
    TEST_ASSERT_EQUAL(set_pixels(), surf.writes);
    surf = {};
    band.draw_round_rect(-2, 10, 9, 20, 3, true);
    TEST_ASSERT_TRUE(surf.writes > 0);
    TEST_ASSERT_EQUAL(set_pixels(), surf.writes);
    surf = {};
    band.fill_triangle(-5, 10, 20, 18, 3, 30, true);
    TEST_ASSERT_TRUE(surf.writes > 0);
    TEST_ASSERT_EQUAL(set_pixels(), surf.writes);
    surf = {};
    band.draw_bitmap(-3, 14, icon, true, false); // covers columns 0-4 of rows 16-21
    TEST_ASSERT_EQUAL(5 * 6, surf.writes);
}

// =============================================================================
// Runtime tests: text ink bounds
// =============================================================================

namespace {

using idfxx::font::ink_bounds;
using idfxx::font::text_ink_bounds;

// Returns the bounding box of fb's set pixels relative to (ox, oy), or
// nullopt when none is set.
std::optional<ink_bounds> inked_box(const mono_framebuffer& fb, long ox, long oy) {
    long left = LONG_MAX;
    long top = LONG_MAX;
    long right = LONG_MIN;
    long bottom = LONG_MIN;
    for (long y = 0; y < static_cast<long>(fb.height()); ++y) {
        for (long x = 0; x < static_cast<long>(fb.width()); ++x) {
            if (fb.get_pixel(x, y)) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < left) {
        return std::nullopt;
    }
    return ink_bounds{
        static_cast<int>(left - ox),
        static_cast<int>(top - oy),
        static_cast<size_t>(right - left + 1),
        static_cast<size_t>(bottom - top + 1),
    };
}

} // namespace

TEST_CASE("gfx text_ink_bounds matches the ink draw_text draws in a proportional font",
          "[idfxx][gfx]") {
    auto fb = make_fb(48, 16);
    // Negative left bearings ('j'), accents above the line ('é'),
    // descenders ('g'), soft hyphens (never drawn), and a missing code point
    // drawn as the replacement glyph, or skipped without one ('Z').
    for (const std::string_view text :
         {"A", "Agéj Agéj A", "j", "é", "g", "jA", "A­A", " A ", "Z", "AZ", "-", "", " ",
          "­"}) {
        fb.clear();
        draw_text(fb, prop_font, 10, 6, text);
        TEST_ASSERT_TRUE(inked_box(fb, 10, 6) == text_ink_bounds(prop_font, text));
        fb.clear();
        draw_text(fb, prop_font_no_replacement, 10, 6, text);
        TEST_ASSERT_TRUE(inked_box(fb, 10, 6) == text_ink_bounds(prop_font_no_replacement, text));
    }
    TEST_ASSERT_FALSE(text_ink_bounds(prop_font, " ­ ").has_value());
    TEST_ASSERT_FALSE(text_ink_bounds(prop_font_no_replacement, "ZZ").has_value());

    // Drawing at (x - left, y - top) puts the ink's top-left pixel at (x, y).
    const auto ink = text_ink_bounds(prop_font, "jAé");
    TEST_ASSERT_TRUE(ink.has_value());
    fb.clear();
    draw_text(fb, prop_font, 20 - ink->left, 5 - ink->top, "jAé");
    TEST_ASSERT_TRUE(inked_box(fb, 0, 0) == (ink_bounds{20, 5, ink->width, ink->height}));
}

TEST_CASE("gfx text_ink_bounds matches the ink of hyphenated text box lines", "[idfxx][gfx]") {
    // Each line from break_lines, drawn in its place by draw_text_box, inks
    // what text_ink_bounds reports for it, the drawn hyphen included.
    const std::string_view text = "AA­AA­Agé j­A";
    auto fb = make_fb(48, 64);
    auto line_fb = make_fb(48, 64);
    size_t hyphenated = 0;
    for (const size_t width : {5, 8, 11, 14}) {
        fb.clear();
        draw_text_box(fb, prop_font, 6, 4, width, text, true);
        size_t index = 0;
        for (const auto& line : idfxx::font::break_lines(prop_font, text, width)) {
            // Keep only this line's rows: its ink reaches one row above its
            // top and stops before the next line's ink begins.
            const long top = 4 + static_cast<long>(index * prop_font.line_height);
            line_fb.clear();
            for (long y = top - 1; y < top + prop_font.line_height - 1; ++y) {
                for (size_t x = 0; x < 48; ++x) {
                    line_fb.set_pixel(x, y, fb.get_pixel(x, y));
                }
            }
            TEST_ASSERT_TRUE(inked_box(line_fb, 6, top) == text_ink_bounds(prop_font, line));
            hyphenated += line.hyphenated ? 1 : 0;
            ++index;
        }
        TEST_ASSERT_TRUE(index > 1);
    }
    TEST_ASSERT_TRUE(hyphenated > 0);
}

TEST_CASE("gfx text_ink_bounds clamps ink past the int range", "[idfxx][gfx]") {
    // A hyphenated line as wide as a 32-bit size_t puts its '-' past
    // INT_MAX: the box is clamped to the int range, and ink wholly past it
    // leaves nothing.
    using idfxx::font::text_line;
    const size_t far = UINT32_MAX;
    TEST_ASSERT_FALSE(text_ink_bounds(prop_font, text_line{"", far, true}).has_value());

    const auto a = text_ink_bounds(prop_font, "A");
    const auto hyphen = text_ink_bounds(prop_font, "-");
    TEST_ASSERT_TRUE(a.has_value() && hyphen.has_value());
    const auto ink = text_ink_bounds(prop_font, text_line{"A", far, true});
    TEST_ASSERT_TRUE(ink.has_value());
    const int top = std::min(a->top, hyphen->top);
    const int bottom = std::max(
        a->top + static_cast<int>(a->height), hyphen->top + static_cast<int>(hyphen->height)
    );
    TEST_ASSERT_EQUAL(a->left, ink->left);
    TEST_ASSERT_EQUAL(top, ink->top);
    TEST_ASSERT_TRUE(ink->width == static_cast<size_t>(INT_MAX - a->left));
    TEST_ASSERT_TRUE(ink->height == static_cast<size_t>(bottom - top));
}

TEST_CASE("gfx text_ink_bounds matches the ink draw_text draws in a fixed-cell font",
          "[idfxx][gfx]") {
    auto fb = make_fb(64, 40);
    for (const std::string_view text :
         {"A", "Hi!", " .", "j_", "'", "  ", "", "\x01\x02", "\x01" "A"}) {
        fb.clear();
        draw_text(fb, spleen_5x8, 3, 2, text);
        TEST_ASSERT_TRUE(inked_box(fb, 3, 2) == text_ink_bounds(spleen_5x8, text));
        // Scaled text inks the unscaled box times the scale.
        fb.clear();
        draw_text(fb, spleen_5x8, 3, 2, text, true, 3);
        const auto unscaled = text_ink_bounds(spleen_5x8, text);
        const auto scaled = inked_box(fb, 3, 2);
        TEST_ASSERT_EQUAL(unscaled.has_value(), scaled.has_value());
        if (unscaled && scaled) {
            TEST_ASSERT_TRUE(
                *scaled ==
                (ink_bounds{
                    unscaled->left * 3, unscaled->top * 3, unscaled->width * 3, unscaled->height * 3
                })
            );
        }
    }
    // A line from a text box inks what its text does.
    for (const auto& line : idfxx::font::break_lines(spleen_5x8, "ab cd", 12)) {
        TEST_ASSERT_TRUE(
            text_ink_bounds(spleen_5x8, line) == text_ink_bounds(spleen_5x8, line.text)
        );
    }
}
