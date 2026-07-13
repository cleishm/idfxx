// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_gfx
// Pure pixel rendering — runs everywhere, including QEMU.

#include "idfxx/font/spleen"
#include "idfxx/gfx"
#include "idfxx/lcd/mono_framebuffer"
#include "idfxx/lcd/rgb565_framebuffer"
#include "unity.h"

#include <array>
#include <concepts>
#include <cstddef>
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

namespace {

// A surface with its own flush, proving the canvas forwarders pass
// arguments and return values through.
struct flushable_surface {
    using pixel_type = int;

    int flushed = -1;

    void set_pixel(size_t, size_t, int) noexcept {}
    [[nodiscard]] size_t width() const noexcept { return 4; }
    [[nodiscard]] size_t height() const noexcept { return 4; }
    int flush(int arg) {
        flushed = arg;
        return arg + 1;
    }
};

// GCC evaluates non-dependent requires-expressions eagerly, turning a failed
// call into a hard error; concepts keep the negative checks SFINAE-friendly.
template<typename C, typename... Args>
concept has_flush = requires(C& c) { c.flush(std::declval<Args>()...); };

template<typename C, typename... Args>
concept has_try_flush = requires(C& c) { c.try_flush(std::declval<Args>()...); };

} // namespace

// A canvas is itself a pixel surface, whatever it wraps.
static_assert(pixel_surface<canvas<mono_framebuffer>>);
static_assert(pixel_surface<canvas<rgb565_framebuffer>>);
static_assert(pixel_surface<canvas<counting_surface>>);

// The flush forwarders exist exactly when the surface has a matching
// method: framebuffers expose try_flush to a panel, counting_surface has
// neither, flushable_surface has flush but not try_flush.
static_assert(has_try_flush<canvas<rgb565_framebuffer>, idfxx::lcd::panel&, size_t, size_t>);
static_assert(has_try_flush<canvas<mono_framebuffer>, idfxx::lcd::panel&>);
static_assert(has_flush<canvas<flushable_surface>, int>);
static_assert(!has_flush<canvas<counting_surface>>);
static_assert(!has_try_flush<canvas<counting_surface>>);
static_assert(!has_try_flush<canvas<flushable_surface>, int>);

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Returns true when the framebuffer pixel matches the glyph bitmap bit for
// every pixel of the glyph cell rendered at (ox, oy) with the given scale.
bool matches_glyph(const mono_framebuffer& fb, const mono_font& f, char c, size_t ox, size_t oy, unsigned scale = 1) {
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

// mono_framebuffer's dimension constructor throws and is compiled out when
// exceptions are disabled; build through make() so the drawing tests compile
// in both configurations.
mono_framebuffer make_fb(size_t width, size_t height) {
    return std::move(*mono_framebuffer::make(width, height));
}

rgb565_framebuffer make_color_fb(size_t width, size_t height) {
    return std::move(*rgb565_framebuffer::make(width, height));
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

TEST_CASE("gfx canvas flush forwards arguments and return value", "[idfxx][gfx]") {
    flushable_surface surface;
    canvas c(surface);
    TEST_ASSERT_EQUAL(43, c.flush(42));
    TEST_ASSERT_EQUAL(42, surface.flushed);
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
    c.draw_text(spleen_8x16, 2, 8, "AB");  // glyph rows straddle y=16
    c.fill_rect(20, 10, 8, 12, true);      // rectangle straddles y=16
    c.draw_line(0, 31, 31, 0, true);       // diagonal crosses both bands
    c.draw_rect(1, 1, 30, 30, true);       // frame with edges in both bands
}

} // namespace

TEST_CASE("gfx translated canvas maps canvas coordinates onto the surface", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    canvas c(fb, 4, 16); // fb's top-left sits at (4, 16)

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
        canvas c(band, 0, band_y);
        c.clear();
        draw_band_test_scene(c);
        for (size_t y = 0; y < band.height(); ++y) {
            for (size_t x = 0; x < band.width(); ++x) {
                TEST_ASSERT_EQUAL(reference.get_pixel(x, band_y + y), band.get_pixel(x, y));
            }
        }
    }
}

TEST_CASE("gfx translated canvas fill and clear cover the whole band", "[idfxx][gfx]") {
    auto fb = make_fb(16, 8);
    canvas c(fb, 0, 24);
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

// =============================================================================
// Runtime tests: render_banded
// =============================================================================

namespace {

// A frame-sized pixel store that band flushes assemble into, standing in
// for a panel when testing try_render_banded end to end.
struct band_target {
    std::array<std::array<bool, 32>, 32> pixels{};
    size_t flushes = 0;
    size_t fail_at = SIZE_MAX; // flush index that reports failure
};

// A 32x16 band surface whose try_flush copies its pixels into a
// band_target at the given offset.
struct flushing_band {
    using pixel_type = bool;

    std::array<std::array<bool, 32>, 16> pixels{};

    void set_pixel(size_t x, size_t y, bool on) noexcept {
        if (x < 32 && y < 16) {
            pixels[y][x] = on;
        }
    }
    [[nodiscard]] size_t width() const noexcept { return 32; }
    [[nodiscard]] size_t height() const noexcept { return 16; }

    [[nodiscard]] idfxx::result<void> try_flush(band_target& target, size_t x, size_t y) const {
        if (target.flushes++ == target.fail_at) {
            return idfxx::error(idfxx::errc::timeout);
        }
        for (size_t row = 0; row < 16; ++row) {
            for (size_t col = 0; col < 32; ++col) {
                target.pixels[y + row][x + col] = pixels[row][col];
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

    flushing_band band;
    band_target target;
    size_t passes = 0;
    auto rendered = try_render_banded(band, target, 32, [&](canvas<flushing_band>& c) {
        ++passes;
        draw_band_test_scene(c);
    });
    TEST_ASSERT_TRUE(rendered.has_value());
    TEST_ASSERT_EQUAL(2, passes);
    TEST_ASSERT_EQUAL(2, target.flushes);

    for (size_t y = 0; y < 32; ++y) {
        for (size_t x = 0; x < 32; ++x) {
            TEST_ASSERT_EQUAL(reference.get_pixel(x, y), target.pixels[y][x]);
        }
    }
}

TEST_CASE("gfx render_banded rejects a frame height the band does not tile", "[idfxx][gfx]") {
    flushing_band band;
    band_target target;
    auto draw = [](canvas<flushing_band>&) {};

    auto not_multiple = try_render_banded(band, target, 24, draw);
    TEST_ASSERT_FALSE(not_multiple.has_value());
    TEST_ASSERT_TRUE(idfxx::errc::invalid_arg == not_multiple.error());

    auto zero = try_render_banded(band, target, 0, draw);
    TEST_ASSERT_FALSE(zero.has_value());
    TEST_ASSERT_TRUE(idfxx::errc::invalid_arg == zero.error());

    TEST_ASSERT_EQUAL(0, target.flushes);
}

TEST_CASE("gfx render_banded stops and reports a failed flush", "[idfxx][gfx]") {
    flushing_band band;
    band_target target;
    target.fail_at = 1; // second flush fails

    size_t passes = 0;
    auto rendered = try_render_banded(band, target, 64, [&](canvas<flushing_band>&) { ++passes; });
    TEST_ASSERT_FALSE(rendered.has_value());
    TEST_ASSERT_TRUE(idfxx::errc::timeout == rendered.error());
    TEST_ASSERT_EQUAL(2, passes); // first band flushed, second failed, no further passes
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("gfx render_banded throws on error", "[idfxx][gfx]") {
    flushing_band band;
    band_target target;

    render_banded(band, target, 32, [](canvas<flushing_band>&) {}); // no throw on success

    bool thrown = false;
    try {
        render_banded(band, target, 24, [](canvas<flushing_band>&) {});
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
