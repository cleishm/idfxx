// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_gfx
// Pure pixel rendering — runs everywhere, including QEMU.

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

#include <array>
#include <concepts>
#include <cstddef>
#include <initializer_list>
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
// These verify correctness at compile time - if this file compiles, they pass.
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
// exceptions are disabled; build through make() so the drawing tests compile
// in both configurations.
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
// [dx, dx + 32); true when every band equals its part of the frame and the
// frame has ink.
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
    // glyph drawn natively and rotated by hand — the two mappings must
    // agree for every pixel of the frame.
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

    // Through a canvas too: full coverage takes the surface's native path.
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

// A frame-sized pixel store that band writes assemble into, standing in
// for a panel when testing try_render_banded end to end.
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
    // Must not crash; pixels inside the fb may be set, nothing to assert
    // beyond bounds-safety (get_pixel outside is false by contract).
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

// A tiny proportional font with ascent 3: 'A' is a 2x2 block on the
// baseline, 'g' a 1x3 stroke descending two rows below it, 'j' a pixel
// overhanging one column left of the pen, U+00E9 a pixel above the line
// top, '?' (the replacement) a pixel on the baseline row, '-' a 1x1 dash
// just above the baseline, and U+00AD an inked glyph that must never be drawn.
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
