// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/gfx>
 * @file gfx.hpp
 * @brief Drawing primitives for pixel surfaces.
 *
 * @defgroup idfxx_gfx Graphics Component
 * @brief Shapes, lines, bitmaps and text drawing on any pixel surface.
 *
 * Provides integer drawing primitives for any type that satisfies the
 * @ref idfxx::gfx::pixel_surface concept. Any type with `set_pixel(x, y, pixel)`
 * and reported dimensions can be drawn on. The ink value has the surface's
 * pixel type: `bool` for monochrome framebuffers and `idfxx::lcd::rgb565` for
 * color ones. The primitives are rectangles (plain and rounded), ellipses and
 * circles, triangles, lines, 1-bit bitmaps, and text in fixed-cell and
 * proportional fonts. Drawing writes only ink: it writes the requested pixels
 * and leaves all other pixels unchanged, so new drawing goes on top of the
 * existing content. Only an opaque @ref idfxx::gfx::draw_bitmap also writes
 * the pixels under its clear bits.
 *
 * Positions are signed (`int`) and sizes are unsigned (`size_t`). A shape or
 * a line of text can start or extend past any edge of the surface, for
 * example scrolling text or a sprite that slides in from the left. Only its
 * visible part is drawn, and the caller does not need to clip it. Drawing
 * never writes column or row `INT_MAX` or beyond. Canvases and windows clamp
 * their coordinate spaces to at most `INT_MAX` columns and rows. Drawing on a
 * surface that is wider or taller than that is clipped at that limit.
 *
 * The primitives are available two ways: as members of @ref
 * idfxx::gfx::canvas, a lightweight view that combines a surface with the
 * drawing primitives, and as free functions that take the surface as their
 * first argument. @ref idfxx::gfx::rotated adapts a surface that is mounted a
 * quarter turn from the drawing layout, for example a portrait panel that is
 * used in landscape orientation.
 *
 * @code
 * idfxx::lcd::mono_framebuffer fb(display.width(), display.height());
 * idfxx::gfx::canvas canvas(fb);
 *
 * canvas.fill_rect(0, 0, canvas.width(), 10, true);
 * canvas.draw_text(idfxx::font::spleen_5x8, 2, 1, "status", false);
 * canvas.draw_text(idfxx::font::spleen_8x16, 4, 16, "23.7", true, 2);
 * display.write(fb);
 * @endcode
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error.hpp>
#include <idfxx/font.hpp>

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

/**
 * @headerfile <idfxx/gfx>
 * @brief Drawing primitives and the pixel surface concept.
 */
namespace idfxx::gfx {

/**
 * @headerfile <idfxx/gfx>
 * @brief A drawable two-dimensional pixel surface.
 *
 * A pixel surface names its pixel value type (`pixel_type`), reports its
 * dimensions, and accepts pixel writes at (x, y) coordinates with (0, 0) the
 * top-left corner. Writes outside `[0, width()) x [0, height())` must be
 * ignored. Drawing functions rely on this for clipping.
 *
 * `idfxx::lcd::mono_framebuffer` (`pixel_type` = `bool`) and
 * `idfxx::lcd::rgb565_framebuffer` (`pixel_type` = `idfxx::lcd::rgb565`)
 * both satisfy this concept.
 *
 * @tparam S The surface type.
 */
template<typename S>
concept pixel_surface =
    requires(S& s, const S& cs, size_t x, size_t y, typename S::pixel_type pixel) {
        typename S::pixel_type;
        { s.set_pixel(x, y, pixel) } noexcept;
        { cs.width() } -> std::convertible_to<size_t>;
        { cs.height() } -> std::convertible_to<size_t>;
    };

/** @cond INTERNAL */
namespace detail {

// Signed type for drawing arithmetic: holds any sum or difference of int
// positions, size_t extents clamped to a clip, and clip coordinates, so
// positions near the int limits never overflow.
//
// The 64-bit arithmetic is in the non-template helpers below. These helpers
// are kept out of line, so that the drawing templates (instantiated for each
// surface type) operate on size_t values that are already clipped.
using wide = std::int64_t;

// The bound that every drawable rectangle stays within. clip_of clamps a
// plain surface's rectangle to [0, coordinate_limit). Canvases and windows
// clamp their coordinate spaces to it. Rotated views clamp their turned
// rectangle to it. So a position saturated to INT_MAX is off every surface,
// and no clip coordinate is more than 2^32 - 1 from an int position.
inline constexpr size_t coordinate_limit = static_cast<size_t>(std::numeric_limits<int>::max());

// Returns an extent clamped to coordinate_limit.
[[nodiscard]] constexpr size_t limit_extent(size_t v) noexcept {
    return std::min(v, coordinate_limit);
}

// Returns v clamped to the int range. Positions computed past the int range
// lie beyond every drawable rectangle (below 0, or at or past
// coordinate_limit), so clamping keeps them off-surface.
[[nodiscard]] constexpr int saturate(wide v) noexcept {
    return static_cast<int>(
        std::clamp<wide>(v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max())
    );
}

// Returns x + delta clamped to the int range.
[[nodiscard, gnu::noinline]] inline int offset_by(int x, wide delta) noexcept {
    return saturate(wide{x} + delta);
}

// A half-open interval [begin, end). It is empty when begin >= end.
struct interval {
    size_t begin;
    size_t end;
};

// Returns the part of [pos, pos + length) inside [lo, hi).
[[nodiscard, gnu::noinline]] inline interval
clip_span(wide pos, uint64_t length, size_t lo, size_t hi) noexcept {
    const wide h = static_cast<wide>(hi);
    if (pos >= h) {
        return {};
    }
    const wide room = h - pos; // > 0: columns from pos up to hi
    const wide e = length >= static_cast<uint64_t>(room) ? h : pos + static_cast<wide>(length);
    const wide b = std::max(pos, static_cast<wide>(lo));
    if (b >= e) {
        return {};
    }
    return {static_cast<size_t>(b), static_cast<size_t>(e)};
}

// The visible part of a run of `count` units of `scale` pixels each (glyph
// columns or rows) that starts at a signed position. The visible pixels are
// offsets [lo, hi) from the position. In size_t arithmetic they are at
// base + lo onward, where base holds the position's bits, so that the sum
// wraps back into range. Units [first, last) have at least one visible pixel.
struct unit_span {
    size_t base;
    size_t lo;
    size_t hi;
    size_t first;
    size_t last;
};

// Returns the part of a run of count units of scale pixels at pos inside [lo, hi).
//
// The offsets are at most hi - pos and at most count * scale. Every caller
// passes either pos >= INT_MIN or a glyph-sized run. hi stays within
// coordinate_limit. So the offsets are below 2^32 and fit a 32-bit size_t.
[[nodiscard, gnu::noinline]] inline unit_span
clip_units(wide pos, size_t count, unsigned scale, size_t lo, size_t hi) noexcept {
    const interval v = clip_span(pos, static_cast<uint64_t>(count) * scale, lo, hi);
    if (v.begin >= v.end) {
        return {};
    }
    const size_t vis_lo = static_cast<size_t>(static_cast<wide>(v.begin) - pos);
    const size_t vis_hi = static_cast<size_t>(static_cast<wide>(v.end) - pos);
    return {
        static_cast<size_t>(pos),
        vis_lo,
        vis_hi,
        vis_lo / scale,
        static_cast<size_t>((static_cast<uint64_t>(vis_hi) + scale - 1) / scale),
    };
}

// Returns a canvas's bounds [lo, hi) in the coordinates of a window of
// `size` whose origin is at pos on that canvas, clamped to [0, size) and
// to coordinate_limit.
[[nodiscard, gnu::noinline]] inline interval
window_bounds(int pos, size_t size, size_t lo, size_t hi) noexcept {
    size = limit_extent(size);
    const auto clamp = [](wide v, size_t max) {
        return v <= 0 ? size_t{0}
                      : (static_cast<uint64_t>(v) >= max ? max : static_cast<size_t>(v));
    };
    const size_t end = clamp(static_cast<wide>(hi) - pos, size);
    return {clamp(static_cast<wide>(lo) - pos, end), end};
}

// The half-open rectangle [left, right) x [top, bottom) of a surface's
// coordinates in which a write can change a pixel. It is empty when
// right <= left or bottom <= top. Drawing functions skip work outside it.
struct clip_rect {
    size_t left;
    size_t top;
    size_t right;
    size_t bottom;

    [[nodiscard]] constexpr bool empty() const noexcept { return right <= left || bottom <= top; }

    // Returns the part of the width x height rectangle at (x, y) inside this
    // one, or an empty rectangle.
    [[nodiscard]] clip_rect intersect(int x, int y, size_t width, size_t height) const noexcept {
        const interval cols = clip_span(x, width, left, right);
        const interval rows = clip_span(y, height, top, bottom);
        if (cols.begin >= cols.end || rows.begin >= rows.end) {
            return {};
        }
        return {cols.begin, rows.begin, cols.end, rows.end};
    }
};

// The visible columns and rows of a packed 1-bit bitmap (a mono_bitmap or a
// proportional glyph).
struct bitmap_spans {
    unit_span cols;
    unit_span rows;
};

// Returns the visible part of a width x height bitmap with its top-left
// pixel at (x, y). Both spans are empty when no pixel is visible. The
// position is wide so that text can place glyphs past the int range exactly.
// Callers pass either an int position or a glyph-sized bitmap (see
// clip_units).
[[nodiscard, gnu::noinline]] inline bitmap_spans
clip_bitmap(const clip_rect& clip, wide x, wide y, size_t width, size_t height) noexcept {
    const unit_span cols = clip_units(x, width, 1, clip.left, clip.right);
    if (cols.first >= cols.last) {
        return {};
    }
    return {cols, clip_units(y, height, 1, clip.top, clip.bottom)};
}

// Returns the drawable rectangle of a canvas, in its coordinates: its
// bounds [left, right) x [top, bottom) intersected with its surface's
// drawable rectangle placed at (dx, dy).
[[nodiscard, gnu::noinline]] inline clip_rect translate_clip(
    const clip_rect& inner,
    wide dx,
    wide dy,
    size_t left,
    size_t top,
    size_t right,
    size_t bottom
) noexcept {
    const auto bound = [](wide offset, size_t edge, size_t lo, size_t hi) {
        return static_cast<size_t>(std::clamp<wide>(
            offset + static_cast<wide>(edge), static_cast<wide>(lo), static_cast<wide>(hi)
        ));
    };
    return {
        bound(dx, inner.left, left, right),
        bound(dy, inner.top, top, bottom),
        bound(dx, inner.right, left, right),
        bound(dy, inner.bottom, top, bottom),
    };
}

// True when a surface of width x height placed at (dx, dy) lies wholly
// inside the bounds [left, right) x [top, bottom).
[[nodiscard, gnu::noinline]] inline bool covers(
    wide dx,
    wide dy,
    size_t width,
    size_t height,
    size_t left,
    size_t top,
    size_t right,
    size_t bottom
) noexcept {
    return dx >= static_cast<wide>(left) && dy >= static_cast<wide>(top) &&
        dx + static_cast<wide>(width) <= static_cast<wide>(right) &&
        dy + static_cast<wide>(height) <= static_cast<wide>(bottom);
}

// Returns r clamped to [0, coordinate_limit) on both axes.
[[nodiscard, gnu::noinline]] inline clip_rect limit_clip(const clip_rect& r) noexcept {
    return {
        limit_extent(r.left), limit_extent(r.top), limit_extent(r.right), limit_extent(r.bottom)
    };
}

// Returns the drawable rectangle of a surface. If argument-dependent lookup
// finds a gfx_clip_rect(surface), the result is the rectangle that it
// returns, which is already within coordinate_limit. Otherwise the result is
// the whole surface, clamped to coordinate_limit. A canvas is an example of
// the first case: its gfx_clip_rect returns the part of its coordinate space
// that is backed by its surface.
template<pixel_surface Surface>
[[nodiscard]] constexpr clip_rect clip_of(const Surface& surface) noexcept {
    if constexpr (requires {
                      { gfx_clip_rect(surface) } noexcept -> std::same_as<clip_rect>;
                  }) {
        return gfx_clip_rect(surface);
    } else {
        return {
            0,
            0,
            limit_extent(static_cast<size_t>(surface.width())),
            limit_extent(static_cast<size_t>(surface.height())),
        };
    }
}

} // namespace detail
/** @endcond */

/**
 * @brief Fills a rectangle with the given ink.
 *
 * The rectangle's top-left corner is at (@p x, @p y) and it spans @p width
 * columns and @p height rows. Any part falling outside the surface is
 * clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the rectangle, in pixels; may be negative or off the surface.
 * @param y       Top edge of the rectangle, in pixels; may be negative or off the surface.
 * @param width   Width of the rectangle, in pixels.
 * @param height  Height of the rectangle, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void fill_rect(
    Surface& surface,
    int x,
    int y,
    size_t width,
    size_t height,
    typename Surface::pixel_type ink
) noexcept {
    const detail::clip_rect r = detail::clip_of(surface).intersect(x, y, width, height);
    for (size_t py = r.top; py < r.bottom; ++py) {
        for (size_t px = r.left; px < r.right; ++px) {
            surface.set_pixel(px, py, ink);
        }
    }
}

/**
 * @brief Draws a horizontal line with the given ink.
 *
 * The line starts at (@p x, @p y) and extends @p length pixels to the
 * right. Any part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Column of the line's left end, in pixels; may be negative or off the surface.
 * @param y       Row of the line, in pixels; may be negative or off the surface.
 * @param length  Length of the line, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_hline(
    Surface& surface,
    int x,
    int y,
    size_t length,
    typename Surface::pixel_type ink
) noexcept {
    fill_rect(surface, x, y, length, 1, ink);
}

/**
 * @brief Draws a vertical line with the given ink.
 *
 * The line starts at (@p x, @p y) and extends @p length pixels downward.
 * Any part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Column of the line, in pixels; may be negative or off the surface.
 * @param y       Row of the line's top end, in pixels; may be negative or off the surface.
 * @param length  Length of the line, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_vline(
    Surface& surface,
    int x,
    int y,
    size_t length,
    typename Surface::pixel_type ink
) noexcept {
    fill_rect(surface, x, y, 1, length, ink);
}

/** @cond INTERNAL */
namespace detail {

// Outlines the width x height rectangle at (x, y) as at most four filled
// rectangles, passing each to fill(x, y, width, height). Edges whose
// position overflows int are passed clamped to INT_MAX, which lies past
// every drawable rectangle (see coordinate_limit).
template<typename Fill>
constexpr void outline_rect(int x, int y, size_t width, size_t height, Fill&& fill) {
    if (width == 0 || height == 0) {
        return;
    }
    if (width <= 2 || height <= 2) {
        fill(x, y, width, height);
        return;
    }
    const int inner_y = offset_by(y, 1);
    fill(x, y, width, size_t{1});
    fill(x, offset_by(y, static_cast<wide>(height - 1)), width, size_t{1});
    fill(x, inner_y, size_t{1}, height - 2);
    fill(offset_by(x, static_cast<wide>(width - 1)), inner_y, size_t{1}, height - 2);
}

} // namespace detail
/** @endcond */

/**
 * @brief Outlines a rectangle with the given ink.
 *
 * The rectangle's top-left corner is at (@p x, @p y) and it spans @p width
 * columns and @p height rows. Only its one-pixel border is drawn. Any part
 * falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the rectangle, in pixels; may be negative or off the surface.
 * @param y       Top edge of the rectangle, in pixels; may be negative or off the surface.
 * @param width   Width of the rectangle, in pixels.
 * @param height  Height of the rectangle, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_rect(
    Surface& surface,
    int x,
    int y,
    size_t width,
    size_t height,
    typename Surface::pixel_type ink
) noexcept {
    detail::outline_rect(x, y, width, height, [&](int rx, int ry, size_t rw, size_t rh) {
        fill_rect(surface, rx, ry, rw, rh, ink);
    });
}

/** @cond INTERNAL */
namespace detail {

// A step of a line walk: the steps taken along the line's major (longer)
// axis and along its minor axis, and the walk's error term there as seen
// from the major axis (negated when the major axis is y).
struct line_step {
    uint64_t major;
    uint64_t minor;
    wide err;
};

// Returns the first step of a line walk that can draw inside a clip. The
// line has major_len steps along its major axis and minor_len (<= major_len)
// steps along its minor axis. It reaches the clip's near edges after
// major_skip major steps and minor_skip minor steps. The walk's step k is at
// minor step floor((2 k minor_len + major_len) / (2 major_len)). With both
// lengths below 2^32, every product here stays below 2^64. A major step past
// major_len means the line ends before reaching the clip.
[[nodiscard]] constexpr line_step skip_line(
    uint64_t major_len,
    uint64_t minor_len,
    uint64_t major_skip,
    uint64_t minor_skip
) noexcept {
    if (minor_skip > minor_len) {
        return {major_len + 1, 0, 0};
    }
    uint64_t k = major_skip;
    if (minor_skip > 0) {
        // The smallest k with k * minor_len >= minor_skip * major_len - floor(major_len / 2).
        const uint64_t target = minor_skip * major_len - major_len / 2;
        k = std::max(k, target / minor_len + (target % minor_len != 0 ? 1 : 0));
    }
    if (k > major_len || major_len == 0) {
        return {k, 0, 0};
    }
    const uint64_t q = k * minor_len;
    const uint64_t rem = q % major_len;
    const uint64_t round = 2 * rem >= major_len ? 1 : 0;
    return {
        k,
        q / major_len + round,
        static_cast<wide>(major_len - minor_len) - static_cast<wide>(rem) +
            static_cast<wide>(round * major_len),
    };
}

// A pixel of a line walk, inside the clip.
struct line_pixel {
    size_t x;
    size_t y;
};

// The state of a Bresenham walk from (px, py) to (ex, ey) within a clip.
struct line_walk {
    wide px;
    wide py;
    wide ex;
    wide ey;
    wide dx;
    wide dy;
    wide sx;
    wide sy;
    wide err;
    wide left;
    wide top;
    wide right;
    wide bottom;
    bool done;
};

// Starts the walk of the line between (x0, y0) and (x1, y1) inclusive,
// clipped to clip. The walk always runs from the first endpoint with
// unclipped stepping, so the pixels drawn never depend on the clip. The walk
// starts at its first step that reaches the clip's near edges, so an
// endpoint far off the surface does not add extra steps.
[[nodiscard, gnu::noinline]] inline line_walk
start_line(const clip_rect& clip, int x0, int y0, int x1, int y1) noexcept {
    line_walk w{};
    w.left = static_cast<wide>(clip.left);
    w.top = static_cast<wide>(clip.top);
    w.right = static_cast<wide>(clip.right);
    w.bottom = static_cast<wide>(clip.bottom);
    if (clip.empty() || std::min(x0, x1) >= w.right || std::max(x0, x1) < w.left ||
        std::min(y0, y1) >= w.bottom || std::max(y0, y1) < w.top) {
        w.done = true; // the endpoints' bounding box lies entirely outside the clip
        return w;
    }
    w.px = x0;
    w.py = y0;
    w.ex = x1;
    w.ey = y1;
    w.dx = std::abs(w.ex - w.px);
    w.dy = -std::abs(w.ey - w.py);
    w.sx = w.px < w.ex ? 1 : -1;
    w.sy = w.py < w.ey ? 1 : -1;
    w.err = w.dx + w.dy;
    const wide skip_x = w.sx > 0 ? w.left - w.px : w.px - (w.right - 1);
    const wide skip_y = w.sy > 0 ? w.top - w.py : w.py - (w.bottom - 1);
    if (skip_x > 0 || skip_y > 0) {
        const uint64_t ux = static_cast<uint64_t>(std::max<wide>(skip_x, 0));
        const uint64_t uy = static_cast<uint64_t>(std::max<wide>(skip_y, 0));
        if (w.dx >= -w.dy) {
            const line_step s = skip_line(w.dx, -w.dy, ux, uy);
            if (s.major > static_cast<uint64_t>(w.dx)) {
                w.done = true; // the line ends before reaching the clip
                return w;
            }
            w.px += w.sx * static_cast<wide>(s.major);
            w.py += w.sy * static_cast<wide>(s.minor);
            w.err = s.err;
        } else {
            const line_step s = skip_line(-w.dy, w.dx, uy, ux);
            if (s.major > static_cast<uint64_t>(-w.dy)) {
                w.done = true; // the line ends before reaching the clip
                return w;
            }
            w.py += w.sy * static_cast<wide>(s.major);
            w.px += w.sx * static_cast<wide>(s.minor);
            w.err = -s.err;
        }
    }
    return w;
}

// Continues a line walk, storing up to max of its next pixels inside the
// clip in out. Returns how many were stored: 0 once the walk is done.
[[nodiscard, gnu::noinline]] inline size_t
walk_line(line_walk& w, line_pixel* out, size_t max) noexcept {
    size_t n = 0;
    while (n < max && !w.done) {
        if (w.px >= w.left && w.px < w.right && w.py >= w.top && w.py < w.bottom) {
            out[n++] = {static_cast<size_t>(w.px), static_cast<size_t>(w.py)};
        } else if ((w.sx > 0 ? w.px >= w.right : w.px < w.left) ||
                   (w.sy > 0 ? w.py >= w.bottom : w.py < w.top)) {
            w.done = true; // past the clip's far edge, so no later pixel is inside it
            break;
        }
        if (w.px == w.ex && w.py == w.ey) {
            w.done = true;
            break;
        }
        const wide e2 = 2 * w.err;
        if (e2 >= w.dy) {
            w.err += w.dy;
            w.px += w.sx;
        }
        if (e2 <= w.dx) {
            w.err += w.dx;
            w.py += w.sy;
        }
    }
    return n;
}

} // namespace detail
/** @endcond */

/**
 * @brief Draws a straight line between two points with the given ink.
 *
 * Both endpoints are inclusive. Any part falling outside the surface is
 * clipped. The pixels drawn inside the surface are the same wherever the
 * endpoints are.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x0      Column of the first endpoint, in pixels; may be negative or off the surface.
 * @param y0      Row of the first endpoint, in pixels; may be negative or off the surface.
 * @param x1      Column of the second endpoint, in pixels; may be negative or off the surface.
 * @param y1      Row of the second endpoint, in pixels; may be negative or off the surface.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_line(
    Surface& surface,
    int x0,
    int y0,
    int x1,
    int y1,
    typename Surface::pixel_type ink
) noexcept {
    detail::line_walk walk = detail::start_line(detail::clip_of(surface), x0, y0, x1, y1);
    detail::line_pixel pixels[16];
    for (size_t n; (n = detail::walk_line(walk, pixels, std::size(pixels))) != 0;) {
        for (size_t i = 0; i < n; ++i) {
            surface.set_pixel(pixels[i].x, pixels[i].y, ink);
        }
    }
}

/** @cond INTERNAL */
namespace detail {

// The pixels of one row of a shape, inside the clip: row y's columns
// [first.begin, first.end) and [second.begin, second.end), either or both
// possibly empty.
struct row_spans {
    size_t y;
    interval first;
    interval second;
};

// Produces a shape's rows one at a time from the state that `state` points
// to. It stores the next row in out and returns true. It returns false once
// the shape has no rows left inside the clip.
using span_source = bool (*)(void* state, row_spans& out) noexcept;

// Writes ink to every pixel of every row that a span source produces. All
// the filled and outlined shapes draw through this one loop, so it is
// instantiated once for each surface type, whatever shapes are drawn.
template<pixel_surface Surface>
void fill_spans(
    Surface& surface,
    span_source next,
    void* state,
    typename Surface::pixel_type ink
) noexcept {
    for (row_spans r; next(state, r);) {
        for (size_t px = r.first.begin; px < r.first.end; ++px) {
            surface.set_pixel(px, r.y, ink);
        }
        for (size_t px = r.second.begin; px < r.second.end; ++px) {
            surface.set_pixel(px, r.y, ink);
        }
    }
}

// An unsigned 128-bit value, for the exact ellipse test.
struct u128 {
    uint64_t hi;
    uint64_t lo;
};

// Returns the full product a * b.
[[nodiscard]] constexpr u128 multiply(uint64_t a, uint64_t b) noexcept {
    const uint64_t a_lo = a & 0xFFFFFFFFu;
    const uint64_t a_hi = a >> 32;
    const uint64_t b_lo = b & 0xFFFFFFFFu;
    const uint64_t b_hi = b >> 32;
    const uint64_t ll = a_lo * b_lo;
    const uint64_t lh = a_lo * b_hi;
    const uint64_t hl = a_hi * b_lo;
    const uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
    return {a_hi * b_hi + (lh >> 32) + (hl >> 32) + (mid >> 32), (mid << 32) | (ll & 0xFFFFFFFFu)};
}

[[nodiscard]] constexpr bool less_equal(const u128& a, const u128& b) noexcept {
    return a.hi < b.hi || (a.hi == b.hi && a.lo <= b.lo);
}

// The largest box side that the ellipse test handles exactly. Longer sides
// are clamped to it. Such sides occur only where size_t is wider than 32
// bits.
inline constexpr uint64_t max_ellipse_side = 0xFFFFFFFFu;

// Returns the inset that marks a row of a w-wide box with no pixel inside.
[[nodiscard]] constexpr uint64_t empty_inset(uint64_t w) noexcept {
    return (w + 1) / 2;
}

// Returns the columns on each side of row j of a w x h box (0 < w, h <=
// max_ellipse_side, j < h) that are outside the ellipse inscribed in it. The
// row's pixels inside are [inset, w - inset). The row has no pixels inside
// when 2 * inset >= w.
//
// Pixel (i, j) is inside when its center is inside or on the ellipse.
// With u = 2i + 1 - w and v = 2j + 1 - h (twice the center's offsets from
// the box's center), that is u^2 h^2 + v^2 w^2 <= w^2 h^2, or
// (u h)^2 <= w^2 (h^2 - v^2) with h^2 - v^2 = (2j + 1) (2h - 2j - 1). Both
// sides are below 2^128 and every factor is below 2^64.
[[nodiscard, gnu::noinline]] inline uint64_t
ellipse_inset(uint64_t w, uint64_t h, uint64_t j) noexcept {
    const u128 room = multiply(w * w, (2 * j + 1) * (2 * h - 2 * j - 1));
    // The largest u in [0, w) with (u h)^2 <= room, found bit by bit (the
    // test holds for every u up to it).
    uint64_t u = 0;
    for (uint64_t bit = std::bit_floor(w); bit != 0; bit >>= 1) {
        const uint64_t t = u | bit;
        if (t < w && less_equal(multiply(t * h, t * h), room)) {
            u = t;
        }
    }
    // u takes the parity of w - 1: step down to the largest such value.
    if (((u ^ (w - 1)) & 1) != 0) {
        if (u == 0) {
            return empty_inset(w); // no pixel of the row is inside
        }
        --u;
    }
    return (w - 1 - u) / 2;
}

// A w x h box with rounded corners at (x, y). Its corners are the quadrants
// of the ellipse inscribed in a cw x ch box (cw <= w, ch <= h). An ellipse
// is the box with cw = w and ch = h. A rectangle is the box with
// cw = ch = 0. Sides are at most max_ellipse_side.
struct round_box {
    wide x;
    wide y;
    uint64_t w;
    uint64_t h;
    uint64_t cw;
    uint64_t ch;
};

// Returns the columns on each side of row j (< b.h) of a round box that are
// outside it. The row's pixels inside are [inset, b.w - inset). The row has
// no pixels inside when 2 * inset >= b.w. The top ch / 2 rows are the rows
// of the corner ellipse's top half. The bottom ch / 2 rows are the rows of
// its bottom half. The rows between them span the whole box.
[[nodiscard, gnu::noinline]] inline uint64_t round_inset(const round_box& b, uint64_t j) noexcept {
    const uint64_t half = b.ch / 2;
    if (j >= half && j < b.h - half) {
        return 0;
    }
    return ellipse_inset(b.cw, b.ch, j < half ? j : j - (b.h - b.ch));
}

// The ellipse inscribed in the width x height box at (x, y).
[[nodiscard]] constexpr round_box ellipse_box(int x, int y, size_t width, size_t height) noexcept {
    const uint64_t w = std::min<uint64_t>(width, max_ellipse_side);
    const uint64_t h = std::min<uint64_t>(height, max_ellipse_side);
    return {x, y, w, h, w, h};
}

// The circle of a radius (clamped to INT_MAX) around (cx, cy): the ellipse
// inscribed in the box of 2 * radius + 1 pixels centered on it.
[[nodiscard]] constexpr round_box circle_box(int cx, int cy, size_t radius) noexcept {
    const uint64_t r = std::min<uint64_t>(radius, static_cast<uint64_t>(coordinate_limit));
    const uint64_t d = 2 * r + 1;
    return {wide{cx} - static_cast<wide>(r), wide{cy} - static_cast<wide>(r), d, d, d, d};
}

// The width x height rectangle at (x, y) with corners of a radius clamped
// to half its shorter side.
[[nodiscard]] constexpr round_box
round_rect_box(int x, int y, size_t width, size_t height, size_t radius) noexcept {
    const uint64_t w = std::min<uint64_t>(width, max_ellipse_side);
    const uint64_t h = std::min<uint64_t>(height, max_ellipse_side);
    const uint64_t r = std::min<uint64_t>(radius, std::min(w, h) / 2);
    return {x, y, w, h, 2 * r, 2 * r};
}

// The state of filling or outlining a round box inside a clip, one row at a
// time. Surface rows [row, end) remain. For outlines, once primed is set,
// above and inset hold the insets of the box rows at row - 1 and row. An
// inset of empty_inset marks a row outside the box.
struct round_walk {
    round_box box;
    size_t left;
    size_t right;
    size_t row;
    size_t end;
    uint64_t above;
    uint64_t inset;
    bool primed;
};

// Starts a walk over the rows of b inside clip. The walk has no rows when
// b's bounding box does not overlap the clip.
[[nodiscard, gnu::noinline]] inline round_walk
start_round(const clip_rect& clip, const round_box& b) noexcept {
    round_walk w{b, clip.left, clip.right, 0, 0, 0, 0, false};
    const interval cols = clip_span(b.x, b.w, clip.left, clip.right);
    const interval rows = clip_span(b.y, b.h, clip.top, clip.bottom);
    if (cols.begin >= cols.end || rows.begin >= rows.end) {
        return w;
    }
    w.row = rows.begin;
    w.end = rows.end;
    return w;
}

// Starts a walk over the ellipse inscribed in the width x height box at (x, y).
[[nodiscard, gnu::noinline]] inline round_walk
start_ellipse(const clip_rect& clip, int x, int y, size_t width, size_t height) noexcept {
    return start_round(clip, ellipse_box(x, y, width, height));
}

// Starts a walk over the circle of a radius around (cx, cy).
[[nodiscard, gnu::noinline]] inline round_walk
start_circle(const clip_rect& clip, int cx, int cy, size_t radius) noexcept {
    return start_round(clip, circle_box(cx, cy, radius));
}

// Starts a walk over the width x height rectangle at (x, y) with rounded corners.
[[nodiscard, gnu::noinline]] inline round_walk start_round_rect(
    const clip_rect& clip,
    int x,
    int y,
    size_t width,
    size_t height,
    size_t radius
) noexcept {
    return start_round(clip, round_rect_box(x, y, width, height, radius));
}

// Returns the columns [inset, b.w - inset) of b's rows, clipped to [left, right).
[[nodiscard]] inline interval
clip_inset(const round_box& b, uint64_t inset, size_t left, size_t right) noexcept {
    if (2 * inset >= b.w) {
        return {};
    }
    return clip_span(b.x + static_cast<wide>(inset), b.w - 2 * inset, left, right);
}

// The span_source of a filled round box (state: a round_walk).
[[nodiscard, gnu::noinline]] inline bool next_round_fill(void* state, row_spans& out) noexcept {
    round_walk& w = *static_cast<round_walk*>(state);
    if (w.row >= w.end) {
        return false;
    }
    const uint64_t j = static_cast<uint64_t>(static_cast<wide>(w.row) - w.box.y);
    out = {w.row, clip_inset(w.box, round_inset(w.box, j), w.left, w.right), {}};
    ++w.row;
    return true;
}

// The span_source of an outlined round box (state: a round_walk). It
// produces each row's pixels that are inside the box and have a horizontal
// or vertical neighbor outside it. The neighbors come from the box's
// geometry, not the clip.
[[nodiscard, gnu::noinline]] inline bool next_round_outline(void* state, row_spans& out) noexcept {
    round_walk& w = *static_cast<round_walk*>(state);
    if (w.row >= w.end) {
        return false;
    }
    const round_box& b = w.box;
    const uint64_t j = static_cast<uint64_t>(static_cast<wide>(w.row) - b.y);
    if (!w.primed) {
        // On the walk's first row, look up this row and the row above it.
        w.above = j != 0 ? round_inset(b, j - 1) : empty_inset(b.w);
        w.inset = round_inset(b, j);
        w.primed = true;
    }
    const uint64_t below = j + 1 < b.h ? round_inset(b, j + 1) : empty_inset(b.w);
    const uint64_t a = w.inset;
    // Pixels [m, b.w - m) have all four neighbors inside.
    const uint64_t m = std::max({a + 1, w.above, below});
    out.y = w.row;
    if (2 * m >= b.w) {
        out.first = clip_inset(b, a, w.left, w.right);
        out.second = {};
    } else {
        out.first = clip_span(b.x + static_cast<wide>(a), m - a, w.left, w.right);
        out.second = clip_span(b.x + static_cast<wide>(b.w - m), m - a, w.left, w.right);
    }
    w.above = a;
    w.inset = below;
    ++w.row;
    return true;
}

} // namespace detail
/** @endcond */

/**
 * @brief Fills an ellipse with the given ink.
 *
 * Fills the ellipse inscribed in the box whose top-left corner is at
 * (@p x, @p y) and which spans @p width columns and @p height rows. A pixel
 * of the box is filled when its center is inside the ellipse or on it. The
 * test is exact, so the ellipse is symmetric about both of the box's center
 * lines. Boxes of even and odd sizes work the same way. An even-sized box
 * has no center pixel. In a very thin box, the end rows or columns can be
 * empty, because no pixel center in them is inside the ellipse. A zero
 * @p width or @p height draws nothing. Any part falling outside the surface
 * is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the bounding box, in pixels; may be negative or off the surface.
 * @param y       Top edge of the bounding box, in pixels; may be negative or off the surface.
 * @param width   Width of the bounding box, in pixels.
 * @param height  Height of the bounding box, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void fill_ellipse(
    Surface& surface,
    int x,
    int y,
    size_t width,
    size_t height,
    typename Surface::pixel_type ink
) noexcept {
    detail::round_walk walk = detail::start_ellipse(detail::clip_of(surface), x, y, width, height);
    detail::fill_spans(surface, &detail::next_round_fill, &walk, ink);
}

/**
 * @brief Outlines an ellipse with the given ink.
 *
 * Draws the one-pixel boundary of the ellipse that @ref fill_ellipse fills
 * with the same arguments. The boundary is the filled pixels that have a
 * horizontal or vertical neighbor that is not filled. The outline has no
 * gaps. Filling the same ellipse covers it exactly. A zero @p width or
 * @p height draws nothing. Any part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the bounding box, in pixels; may be negative or off the surface.
 * @param y       Top edge of the bounding box, in pixels; may be negative or off the surface.
 * @param width   Width of the bounding box, in pixels.
 * @param height  Height of the bounding box, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_ellipse(
    Surface& surface,
    int x,
    int y,
    size_t width,
    size_t height,
    typename Surface::pixel_type ink
) noexcept {
    detail::round_walk walk = detail::start_ellipse(detail::clip_of(surface), x, y, width, height);
    detail::fill_spans(surface, &detail::next_round_outline, &walk, ink);
}

/**
 * @brief Fills a circle with the given ink.
 *
 * Fills the circle of @p radius pixels around the pixel (@p cx, @p cy). This
 * circle is defined as the ellipse that @ref fill_ellipse fills in the
 * square box of `2 * radius + 1` pixels centered on that pixel. So its
 * pixels are those whose centers are within `radius + 0.5` of the center
 * pixel's center. A radius of 0 fills only the center pixel. A radius of 1
 * fills the 3 x 3 square around it. A radius past `INT_MAX` is treated as
 * `INT_MAX`. Any part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param cx      Column of the center pixel; may be negative or off the surface.
 * @param cy      Row of the center pixel; may be negative or off the surface.
 * @param radius  Radius, in pixels, from the center pixel to the edge pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void fill_circle(
    Surface& surface,
    int cx,
    int cy,
    size_t radius,
    typename Surface::pixel_type ink
) noexcept {
    detail::round_walk walk = detail::start_circle(detail::clip_of(surface), cx, cy, radius);
    detail::fill_spans(surface, &detail::next_round_fill, &walk, ink);
}

/**
 * @brief Outlines a circle with the given ink.
 *
 * Draws the one-pixel boundary of the circle that @ref fill_circle fills
 * with the same arguments. This is the outline that @ref draw_ellipse draws
 * in the square box of `2 * radius + 1` pixels centered on (@p cx, @p cy).
 * A radius of 0 draws only the center pixel. A radius past `INT_MAX` is
 * treated as `INT_MAX`. Any part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param cx      Column of the center pixel; may be negative or off the surface.
 * @param cy      Row of the center pixel; may be negative or off the surface.
 * @param radius  Radius, in pixels, from the center pixel to the edge pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_circle(
    Surface& surface,
    int cx,
    int cy,
    size_t radius,
    typename Surface::pixel_type ink
) noexcept {
    detail::round_walk walk = detail::start_circle(detail::clip_of(surface), cx, cy, radius);
    detail::fill_spans(surface, &detail::next_round_outline, &walk, ink);
}

/**
 * @brief Fills a rectangle with rounded corners with the given ink.
 *
 * The rectangle's top-left corner is at (@p x, @p y) and it spans @p width
 * columns and @p height rows. Each corner is a quadrant of the ellipse that
 * @ref fill_ellipse fills in a `2 * radius` square box. So
 * `fill_round_rect(s, x, y, 2 * r, 2 * r, r, ink)` fills the same pixels as
 * `fill_ellipse(s, x, y, 2 * r, 2 * r, ink)`. A radius of 0 fills the plain
 * rectangle. The radius is clamped to half the shorter side. Small radii
 * have little effect. A radius of 1 removes no pixel from the corners. Radii
 * of 2 and 3 remove one pixel from each corner. A zero @p width or
 * @p height draws nothing. Any part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the rectangle, in pixels; may be negative or off the surface.
 * @param y       Top edge of the rectangle, in pixels; may be negative or off the surface.
 * @param width   Width of the rectangle, in pixels.
 * @param height  Height of the rectangle, in pixels.
 * @param radius  Corner radius, in pixels; clamped to `min(width, height) / 2`.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void fill_round_rect(
    Surface& surface,
    int x,
    int y,
    size_t width,
    size_t height,
    size_t radius,
    typename Surface::pixel_type ink
) noexcept {
    detail::round_walk walk =
        detail::start_round_rect(detail::clip_of(surface), x, y, width, height, radius);
    detail::fill_spans(surface, &detail::next_round_fill, &walk, ink);
}

/**
 * @brief Outlines a rectangle with rounded corners with the given ink.
 *
 * Draws the one-pixel boundary of the rounded rectangle that
 * @ref fill_round_rect fills with the same arguments. The boundary is the
 * filled pixels that have a horizontal or vertical neighbor that is not
 * filled. The outline has no gaps. With a radius of 0, it is the outline
 * that @ref draw_rect draws. A zero @p width or @p height draws nothing. Any
 * part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the rectangle, in pixels; may be negative or off the surface.
 * @param y       Top edge of the rectangle, in pixels; may be negative or off the surface.
 * @param width   Width of the rectangle, in pixels.
 * @param height  Height of the rectangle, in pixels.
 * @param radius  Corner radius, in pixels; clamped to `min(width, height) / 2`.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_round_rect(
    Surface& surface,
    int x,
    int y,
    size_t width,
    size_t height,
    size_t radius,
    typename Surface::pixel_type ink
) noexcept {
    detail::round_walk walk =
        detail::start_round_rect(detail::clip_of(surface), x, y, width, height, radius);
    detail::fill_spans(surface, &detail::next_round_outline, &walk, ink);
}

/** @cond INTERNAL */
namespace detail {

// The state of filling a triangle inside a clip, one row at a time. Surface
// rows [row, end) remain. The vertices are sorted by row and, within a row,
// by column (top, middle, bottom). So each edge runs from its upper endpoint
// (the left one for a horizontal edge), in the direction that draw_triangle
// draws it. long_left is true when the top-to-bottom edge bounds the rows on
// the left, that is, when the middle vertex is right of that edge.
struct triangle_walk {
    wide tx;
    wide ty;
    wide mx;
    wide my;
    wide bx;
    wide by;
    wide min_x;
    wide max_x;
    size_t left;
    size_t right;
    size_t row;
    size_t end;
    bool long_left;
};

// True when (x1, y1) comes before (x0, y0) by row, then column. In that case
// a triangle edge between them starts at (x1, y1), its upper endpoint (the
// left one for a horizontal edge). The outline and the fill both draw the
// edge from that endpoint.
[[nodiscard]] constexpr bool edge_reversed(int x0, int y0, int x1, int y1) noexcept {
    return y1 < y0 || (y1 == y0 && x1 < x0);
}

// An inclusive run [first, last] of a row's columns. It is empty when first > last.
struct column_run {
    wide first;
    wide last;
};

// Returns the columns of row y that hold pixels of the line that draw_line
// draws from (x0, y0) to (x1, y1). The result is empty when the line does
// not cross the row. The line's walk steps once along the major (longer)
// axis for each pixel. Its step k is at the minor step that skip_line
// computes for it. So a row at minor step t of an x-major line holds the
// steps from the first step at minor step t up to the step before the first
// step at t + 1. A row of a y-major line holds the one pixel at major step t.
[[nodiscard]] constexpr column_run line_row(wide x0, wide y0, wide x1, wide y1, wide y) noexcept {
    if (y < std::min(y0, y1) || y > std::max(y0, y1)) {
        return {1, 0};
    }
    const uint64_t dx = static_cast<uint64_t>(x1 > x0 ? x1 - x0 : x0 - x1);
    const uint64_t dy = static_cast<uint64_t>(y1 > y0 ? y1 - y0 : y0 - y1);
    const uint64_t t = static_cast<uint64_t>(y > y0 ? y - y0 : y0 - y);
    const wide sx = x0 < x1 ? 1 : -1;
    if (dx < dy) {
        const wide x = x0 + sx * static_cast<wide>(skip_line(dy, dx, t, 0).minor);
        return {x, x};
    }
    uint64_t first = 0;
    uint64_t last = dx;
    if (dy != 0) {
        first = skip_line(dx, dy, 0, t).major;
        last = t == dy ? dx : skip_line(dx, dy, 0, t + 1).major - 1;
    }
    const wide a = x0 + sx * static_cast<wide>(first);
    const wide b = x0 + sx * static_cast<wide>(last);
    return {std::min(a, b), std::max(a, b)};
}

// The columns where an edge crosses a row: the floor and ceiling of its
// exact (rational) column there.
struct edge_crossing {
    wide floor;
    wide ceil;
};

// Returns where the edge from (ax, ay) down to (bx, by), ay < by, crosses
// row y, ay <= y <= by. The offsets are below 2^32, so their product fits
// 64 bits. The sign is handled separately from the magnitude.
[[nodiscard]] constexpr edge_crossing
cross_edge(wide ax, wide ay, wide bx, wide by, wide y) noexcept {
    const uint64_t n = static_cast<uint64_t>(y - ay);
    const uint64_t d = static_cast<uint64_t>(by - ay);
    const wide dx = bx - ax;
    const uint64_t product = n * static_cast<uint64_t>(dx < 0 ? -dx : dx);
    const wide q = static_cast<wide>(product / d); // at most |dx|, since n <= d
    const wide frac = product % d != 0 ? 1 : 0;
    if (dx >= 0) {
        return {ax + q, ax + q + frac};
    }
    return {ax - q - frac, ax - q};
}

// Returns the sign of a * b - c * d, for factors below 2^32 in magnitude
// with b, c >= 0.
[[nodiscard]] constexpr int compare_products(wide a, wide b, wide c, wide d) noexcept {
    const bool p_neg = a < 0;
    const bool q_neg = d < 0;
    const uint64_t p = static_cast<uint64_t>(p_neg ? -a : a) * static_cast<uint64_t>(b);
    const uint64_t q = static_cast<uint64_t>(c) * static_cast<uint64_t>(q_neg ? -d : d);
    // Compare the signed values -p or p against -q or q.
    const auto signed_order = [](bool neg, uint64_t v) { return neg && v != 0 ? -1 : (v != 0); };
    const int ps = signed_order(p_neg, p);
    const int qs = signed_order(q_neg, q);
    if (ps != qs) {
        return ps < qs ? -1 : 1;
    }
    if (p == q) {
        return 0;
    }
    return (p > q) == (ps > 0) ? 1 : -1;
}

// Starts a walk over the rows of the triangle with the given vertices
// inside clip. The walk has no rows when the triangle's bounding box does
// not overlap the clip.
[[nodiscard, gnu::noinline]] inline triangle_walk
start_triangle(const clip_rect& clip, int x0, int y0, int x1, int y1, int x2, int y2) noexcept {
    triangle_walk w{};
    // Sort the vertices by row, then column: equal vertices are the same
    // point, so every order of the same three gives the same walk.
    if (edge_reversed(x0, y0, x1, y1)) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    if (edge_reversed(x1, y1, x2, y2)) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    if (edge_reversed(x0, y0, x1, y1)) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    w.tx = x0;
    w.ty = y0;
    w.mx = x1;
    w.my = y1;
    w.bx = x2;
    w.by = y2;
    w.min_x = std::min({x0, x1, x2});
    w.max_x = std::max({x0, x1, x2});
    w.left = clip.left;
    w.right = clip.right;
    const interval cols =
        clip_span(w.min_x, static_cast<uint64_t>(w.max_x - w.min_x) + 1, clip.left, clip.right);
    const interval rows =
        clip_span(w.ty, static_cast<uint64_t>(w.by - w.ty) + 1, clip.top, clip.bottom);
    if (cols.begin >= cols.end || rows.begin >= rows.end) {
        return w;
    }
    w.row = rows.begin;
    w.end = rows.end;
    // The middle vertex lies right of the top-to-bottom edge when the cross
    // product of that edge and the top-to-middle offset is negative (rows
    // grow downward).
    w.long_left = compare_products(w.bx - w.tx, w.my - w.ty, w.by - w.ty, w.mx - w.tx) < 0;
    return w;
}

// The span_source of a filled triangle (state: a triangle_walk). It
// produces each row's pixels whose centers are inside the triangle or on its
// edges. It also produces the pixels of the triangle's edges as
// draw_triangle draws them: top to middle, middle to bottom and top to
// bottom, each from its upper endpoint. In each row, the columns of the two
// parts overlap or are next to each other, because a line's pixels in a row
// include the floor or the ceiling of the edge's exact column in that row.
// So together they form one run from the leftmost column to the rightmost.
[[nodiscard, gnu::noinline]] inline bool next_triangle(void* state, row_spans& out) noexcept {
    triangle_walk& w = *static_cast<triangle_walk*>(state);
    if (w.row >= w.end) {
        return false;
    }
    const wide y = static_cast<wide>(w.row);
    wide begin = w.min_x;
    wide last = w.max_x;
    if (w.ty != w.by) {
        const edge_crossing along = cross_edge(w.tx, w.ty, w.bx, w.by, y);
        const edge_crossing across = y < w.my ? cross_edge(w.tx, w.ty, w.mx, w.my, y)
            : y > w.my                        ? cross_edge(w.mx, w.my, w.bx, w.by, y)
                                              : edge_crossing{w.mx, w.mx};
        begin = w.long_left ? along.ceil : across.ceil;
        last = w.long_left ? across.floor : along.floor;
    } // else every vertex is on this one row: the row spans them all
    const column_run edges[] = {
        line_row(w.tx, w.ty, w.mx, w.my, y),
        line_row(w.mx, w.my, w.bx, w.by, y),
        line_row(w.tx, w.ty, w.bx, w.by, y),
    };
    for (const column_run& edge : edges) {
        if (edge.first > edge.last) {
            continue;
        }
        if (begin > last) {
            begin = edge.first;
            last = edge.last;
        } else {
            begin = std::min(begin, edge.first);
            last = std::max(last, edge.last);
        }
    }
    out = {w.row, {}, {}};
    if (begin <= last) {
        out.first = clip_span(begin, static_cast<uint64_t>(last - begin) + 1, w.left, w.right);
    }
    ++w.row;
    return true;
}

} // namespace detail
/** @endcond */

/**
 * @brief Fills a triangle with the given ink.
 *
 * Each vertex names a pixel, as a @ref draw_line endpoint does. The
 * triangle's edges run between the centers of those pixels. A pixel is
 * filled when its center is inside the triangle or on one of its edges.
 * Every pixel that @ref draw_triangle draws for the same vertices is also
 * filled. So the fill covers the outline. Every fill pixel that has a
 * horizontal or vertical neighbor outside the fill is an outline pixel. So
 * a filled triangle lines up with a border that @ref draw_triangle draws.
 * Neither the fill nor the outline depends on the order in which the
 * vertices are given. Triangles that share an edge have no gap between
 * them. A degenerate triangle (all vertices on one line) fills the pixels
 * that @ref draw_triangle draws for it. Any part falling outside the surface
 * is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x0      Column of the first vertex, in pixels; may be negative or off the surface.
 * @param y0      Row of the first vertex, in pixels; may be negative or off the surface.
 * @param x1      Column of the second vertex, in pixels; may be negative or off the surface.
 * @param y1      Row of the second vertex, in pixels; may be negative or off the surface.
 * @param x2      Column of the third vertex, in pixels; may be negative or off the surface.
 * @param y2      Row of the third vertex, in pixels; may be negative or off the surface.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void fill_triangle(
    Surface& surface,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    typename Surface::pixel_type ink
) noexcept {
    detail::triangle_walk walk =
        detail::start_triangle(detail::clip_of(surface), x0, y0, x1, y1, x2, y2);
    detail::fill_spans(surface, &detail::next_triangle, &walk, ink);
}

/**
 * @brief Outlines a triangle with the given ink.
 *
 * Draws each of the triangle's three edges as @ref draw_line does. Both
 * endpoints are drawn. Each edge is drawn from its upper endpoint (the left
 * one for a horizontal edge) to the other endpoint, so the pixels drawn do
 * not depend on the order in which the vertices are given. @ref fill_triangle
 * also fills these pixels for the same vertices, so a filled triangle and its
 * outline line up. Any part falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x0      Column of the first vertex, in pixels; may be negative or off the surface.
 * @param y0      Row of the first vertex, in pixels; may be negative or off the surface.
 * @param x1      Column of the second vertex, in pixels; may be negative or off the surface.
 * @param y1      Row of the second vertex, in pixels; may be negative or off the surface.
 * @param x2      Column of the third vertex, in pixels; may be negative or off the surface.
 * @param y2      Row of the third vertex, in pixels; may be negative or off the surface.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_triangle(
    Surface& surface,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    typename Surface::pixel_type ink
) noexcept {
    // Draws each edge from its upper endpoint, or from the left endpoint of a
    // horizontal edge.
    const auto edge = [&surface, ink](int ax, int ay, int bx, int by) {
        if (detail::edge_reversed(ax, ay, bx, by)) {
            std::swap(ax, bx);
            std::swap(ay, by);
        }
        draw_line(surface, ax, ay, bx, by, ink);
    };
    edge(x0, y0, x1, y1);
    edge(x1, y1, x2, y2);
    edge(x2, y2, x0, y0);
}

/**
 * @headerfile <idfxx/gfx>
 * @brief Order of the pixels packed into each byte of a @ref mono_bitmap row.
 */
enum class bit_order : uint8_t {
    /// The most significant bit of each byte is its leftmost pixel (the layout of the font
    /// bitmaps).
    msb_first,
    /// The least significant bit of each byte is its leftmost pixel (the XBM layout, as exported
    /// by GIMP and drawn by u8g2).
    lsb_first,
};

/**
 * @headerfile <idfxx/gfx>
 * @brief A 1-bit image, such as an icon, for @ref draw_bitmap.
 *
 * The image is @ref height rows of @ref bytes_per_row bytes (`ceil(width / 8)`),
 * top row first. Each row's pixels are packed left to right in the given
 * @ref order. Set bits are the image's ink, and clear bits are its
 * background. The padding bits after the last pixel of a row are ignored.
 * The bitmap refers to its data and does not own it, so icon tables can be
 * `constexpr`:
 *
 * @code
 * constexpr uint8_t check_bits[] = {0x01, 0x03, 0x86, 0xCC, 0x78, 0x30};
 * constexpr idfxx::gfx::mono_bitmap check{check_bits, 8, 6};
 * @endcode
 *
 * Drawing never reads past @ref data. When it holds fewer than @ref height
 * complete rows, only the complete rows that it holds are drawn.
 */
struct mono_bitmap {
    /// The pixel rows: @ref height rows of @ref bytes_per_row bytes. Must outlive every draw.
    std::span<const uint8_t> data = {};
    /// Width of the image, in pixels.
    size_t width;
    /// Height of the image, in pixels.
    size_t height;
    /// Order of the pixels within each byte.
    bit_order order = bit_order::msb_first;

    /** @brief Returns the number of bytes in one row: `ceil(width / 8)`. */
    [[nodiscard]] constexpr size_t bytes_per_row() const noexcept {
        return width / 8 + (width % 8 != 0 ? 1 : 0);
    }
};

/** @cond INTERNAL */
namespace detail {

// Returns the visible part of a bitmap drawn with its top-left pixel at
// (x, y), limited to the complete rows that its data holds.
[[nodiscard, gnu::noinline]] inline bitmap_spans
clip_mono_bitmap(const clip_rect& clip, int x, int y, const mono_bitmap& bitmap) noexcept {
    const size_t bpr = bitmap.bytes_per_row();
    const size_t rows = bpr == 0 ? 0 : std::min(bitmap.height, bitmap.data.size() / bpr);
    return clip_bitmap(clip, x, y, bitmap.width, rows);
}

// Writes the visible part of a packed 1-bit bitmap whose rows of bpr bytes
// start at bits. Set bits are written in ink. Clear bits are written in
// *paper unless paper is null. Bit (col % 8) ^ flip of a byte, counting from
// its least significant bit, is pixel col. flip is 7 for MSB-first rows and
// 0 for LSB-first rows. Bitmaps and proportional text both draw through this
// one loop, so it is instantiated once for each surface type.
template<pixel_surface Surface>
void blit_bits(
    Surface& surface,
    const bitmap_spans& spans,
    const uint8_t* bits,
    size_t bpr,
    unsigned flip,
    typename Surface::pixel_type ink,
    const typename Surface::pixel_type* paper
) noexcept {
    const unit_span& cols = spans.cols;
    const unit_span& rows = spans.rows;
    for (size_t row = rows.first; row < rows.last; ++row) {
        const uint8_t* row_bits = bits + row * bpr;
        const size_t py = rows.base + row;
        for (size_t col = cols.first; col < cols.last; ++col) {
            if ((row_bits[col / 8] >> ((col % 8) ^ flip)) & 1u) {
                surface.set_pixel(cols.base + col, py, ink);
            } else if (paper != nullptr) {
                surface.set_pixel(cols.base + col, py, *paper);
            }
        }
    }
}

// Draws a bitmap as draw_bitmap does. Set bits are written in ink, and clear
// bits are written in *paper unless paper is null.
template<pixel_surface Surface>
void draw_bits(
    Surface& surface,
    int x,
    int y,
    const mono_bitmap& bitmap,
    typename Surface::pixel_type ink,
    const typename Surface::pixel_type* paper
) noexcept {
    blit_bits(
        surface,
        clip_mono_bitmap(clip_of(surface), x, y, bitmap),
        bitmap.data.data(),
        bitmap.bytes_per_row(),
        bitmap.order == bit_order::msb_first ? 7 : 0,
        ink,
        paper
    );
}

} // namespace detail
/** @endcond */

/**
 * @brief Draws a 1-bit bitmap's set pixels with the given ink.
 *
 * Places the bitmap's top-left pixel at (@p x, @p y) and writes @p ink
 * where its bits are set. The pixels under its clear bits are not changed,
 * so the image is drawn on top of the existing content. Two bitmaps drawn
 * at one origin in different inks appear as two layers. On a monochrome
 * surface, pass `ink = false` to erase the image's set pixels from a filled
 * area. Any part falling outside the surface is clipped.
 *
 * @code
 * canvas.draw_bitmap(4, 4, battery_outline, black);
 * canvas.draw_bitmap(4, 4, battery_level, red); // second layer, same origin
 * @endcode
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Column of the bitmap's left edge, in pixels; may be negative or off the surface.
 * @param y       Row of the bitmap's top edge, in pixels; may be negative or off the surface.
 * @param bitmap  The bitmap to draw; only the complete rows that its data holds are drawn.
 * @param ink     The pixel value to write for set bits.
 */
template<pixel_surface Surface>
void draw_bitmap(
    Surface& surface,
    int x,
    int y,
    const mono_bitmap& bitmap,
    typename Surface::pixel_type ink
) noexcept {
    detail::draw_bits(surface, x, y, bitmap, ink, nullptr);
}

/**
 * @brief Draws a 1-bit bitmap opaquely, with set pixels in one value and clear pixels in another.
 *
 * Places the bitmap's top-left pixel at (@p x, @p y) and writes every
 * pixel that it covers: @p ink where its bits are set and @p paper where
 * they are clear. These writes replace the existing pixels. Any part
 * falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Column of the bitmap's left edge, in pixels; may be negative or off the surface.
 * @param y       Row of the bitmap's top edge, in pixels; may be negative or off the surface.
 * @param bitmap  The bitmap to draw; only the complete rows that its data holds are drawn.
 * @param ink     The pixel value to write for set bits.
 * @param paper   The pixel value to write for clear bits.
 */
template<pixel_surface Surface>
void draw_bitmap(
    Surface& surface,
    int x,
    int y,
    const mono_bitmap& bitmap,
    typename Surface::pixel_type ink,
    typename Surface::pixel_type paper
) noexcept {
    detail::draw_bits(surface, x, y, bitmap, ink, &paper);
}

/** @cond INTERNAL */
namespace detail {

// Draws fixed-cell text as draw_text does, with the first cell's top-left
// corner at (x, y). The position is wide so a text box can place lines past
// the int range exactly.
template<pixel_surface Surface>
void draw_mono(
    Surface& surface,
    const font::mono_font& font,
    wide x,
    wide y,
    std::string_view text,
    typename Surface::pixel_type ink,
    unsigned scale
) noexcept {
    if (scale == 0) {
        scale = 1;
    }
    const clip_rect clip = clip_of(surface);
    // Font rows with at least one visible output row, the same for every glyph.
    const unit_span rows = clip_units(y, font.height, scale, clip.top, clip.bottom);
    if (rows.first >= rows.last) {
        return; // every glyph row would clip
    }
    const size_t bpr = font.bytes_per_row();
    const wide clip_right = static_cast<wide>(clip.right);
    wide cell_x = x;

    for (char c : text) {
        if (cell_x >= clip_right) {
            break; // text is drawn left to right, so no later glyph is inside the clip
        }
        if (font.contains(c)) {
            // Font columns with at least one visible output column.
            const unit_span cols = clip_units(cell_x, font.width, scale, clip.left, clip.right);
            const uint8_t* glyph = font.glyph(c);
            for (size_t row = rows.first; row < rows.last; ++row) {
                const uint8_t* row_bits = glyph + row * bpr;
                // The row's visible output rows, as offsets from y.
                const size_t oy = row * scale;
                const size_t oy_begin = std::max(oy, rows.lo);
                const size_t oy_end = oy + std::min<size_t>(scale, rows.hi - oy);
                for (size_t col = cols.first; col < cols.last; ++col) {
                    if (!(row_bits[col / 8] & (0x80u >> (col % 8)))) {
                        continue;
                    }
                    const size_t ox = col * scale;
                    const size_t ox_begin = std::max(ox, cols.lo);
                    const size_t ox_end = ox + std::min<size_t>(scale, cols.hi - ox);
                    for (size_t sy = rows.base + oy_begin; sy < rows.base + oy_end; ++sy) {
                        for (size_t sx = cols.base + ox_begin; sx < cols.base + ox_end; ++sx) {
                            surface.set_pixel(sx, sy, ink);
                        }
                    }
                }
            }
        }
        cell_x += static_cast<wide>(font.advance(c)) * scale;
    }
}

} // namespace detail
/** @endcond */

/**
 * @brief Draws text with the given ink.
 *
 * Draws @p text left to right, with its top-left corner at (@p x, @p y).
 * Only the glyphs' ink pixels are written. Background pixels within the
 * cell are not changed, so text is drawn on top of the existing content. On
 * a monochrome surface, pass `ink = false` to erase ink pixels instead, for
 * example for inverse text on a filled banner. Characters outside the
 * font's range advance the cursor without drawing. Pixels falling outside
 * the surface are clipped, so text can start to the left of or above it.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the first glyph cell, in pixels; may be negative or off the
 *                surface.
 * @param y       Top edge of the glyph cells, in pixels; may be negative or off the surface.
 * @param text    The text to draw.
 * @param ink     The pixel value to write for glyph ink.
 * @param scale   Integer magnification factor (>= 1; 0 is treated as 1).
 *                Each font pixel becomes a scale x scale block.
 */
template<pixel_surface Surface>
void draw_text(
    Surface& surface,
    const font::mono_font& font,
    int x,
    int y,
    std::string_view text,
    typename Surface::pixel_type ink,
    unsigned scale = 1
) noexcept {
    detail::draw_mono(surface, font, x, y, text, ink, scale);
}

/**
 * @brief Draws text on a monochrome surface, setting glyph ink pixels.
 *
 * Equivalent to @ref draw_text with `ink = true`. See that function for the
 * full rendering contract.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface with a
 *                 `bool` pixel type).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the first glyph cell, in pixels; may be negative or off the surface.
 * @param y       Top edge of the glyph cells, in pixels; may be negative or off the surface.
 * @param text    The text to draw.
 */
template<pixel_surface Surface>
    requires std::same_as<typename Surface::pixel_type, bool>
void draw_text(
    Surface& surface,
    const font::mono_font& font,
    int x,
    int y,
    std::string_view text
) noexcept {
    draw_text(surface, font, x, y, text, true);
}

/** @cond INTERNAL */
namespace detail {

// Draws one line of proportional text as draw_text does, with the line's
// top-left corner at (x, y). The position is wide so that a text box can
// place lines and hyphens past the int range exactly, because ink that
// extends past their position can still be inside the surface.
template<pixel_surface Surface>
void draw_proportional(
    Surface& surface,
    const font::proportional_font& font,
    wide x,
    wide y,
    std::string_view text,
    typename Surface::pixel_type ink
) noexcept {
    const clip_rect clip = clip_of(surface);
    if (clip.empty()) {
        return;
    }
    const wide baseline = y + font.ascent;
    if (baseline - font.ink_ascent >= static_cast<wide>(clip.bottom) ||
        baseline + font.ink_descent <= static_cast<wide>(clip.top)) {
        return; // no glyph's ink is in the clip rows
    }
    const wide clip_right = static_cast<wide>(clip.right);
    wide pen = x;

    for (char32_t cp : utf8_view(text)) {
        if (pen + font.min_left_bearing >= clip_right) {
            break; // this and every later glyph's ink starts right of the clip
        }
        const font::glyph_metrics* g = font::detail::text_glyph(font, cp);
        if (g == nullptr) {
            continue; // skipped, or a soft hyphen (invisible)
        }
        const bitmap_spans spans =
            clip_bitmap(clip, pen + g->x_offset, baseline + g->y_offset, g->width, g->height);
        pen += g->advance;
        // Glyph rows have the MSB-first layout of a mono_bitmap.
        blit_bits(surface, spans, font.glyph_bitmap(*g), g->bytes_per_row(), 7, ink, nullptr);
    }
}

} // namespace detail
/** @endcond */

/**
 * @brief Draws text in a proportional font with the given ink.
 *
 * Decodes @p text as UTF-8 and draws it as one line, left to right, with
 * the line's top-left corner at (@p x, @p y). The baseline is `font.ascent`
 * rows below @p y. Each glyph moves the pen right by its own advance, so
 * @ref font::text_width gives the width to use when centering or aligning
 * the text. Only the glyphs' ink pixels are written. The background is not
 * changed, so text is drawn on top of the existing content. On a monochrome
 * surface, pass `ink = false` to erase ink pixels instead. Code points that
 * the font does not have are drawn as its replacement glyph, or skipped if
 * it has none. U+00AD SOFT HYPHEN is never drawn and takes no space.
 * Control characters (including newlines) are not interpreted. See
 * @ref draw_text_box for wrapped, multi-line text. Pixels falling outside
 * the surface are clipped, including ink that overhangs the line's left or
 * top edge.
 *
 * @code
 * const auto& f = idfxx::font::inter_48;
 * const size_t w = idfxx::font::text_width(f, "Bonjour, café");
 * idfxx::gfx::draw_text(fb, f, (fb.width() - w) / 2, 100, "Bonjour, café", true);
 * @endcode
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the line (the starting pen position), in pixels; may be negative
 *                or off the surface.
 * @param y       Top edge of the line, in pixels; may be negative or off the surface.
 * @param text    The UTF-8 text to draw.
 * @param ink     The pixel value to write for glyph ink.
 */
template<pixel_surface Surface>
void draw_text(
    Surface& surface,
    const font::proportional_font& font,
    int x,
    int y,
    std::string_view text,
    typename Surface::pixel_type ink
) noexcept {
    detail::draw_proportional(surface, font, x, y, text, ink);
}

/**
 * @brief Draws text in a proportional font on a monochrome surface, setting glyph ink pixels.
 *
 * Equivalent to @ref draw_text with `ink = true`. See that function for the
 * full rendering contract.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface with a
 *                 `bool` pixel type).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the line (the starting pen position), in pixels; may be negative or
 *                off the surface.
 * @param y       Top edge of the line, in pixels; may be negative or off the surface.
 * @param text    The UTF-8 text to draw.
 */
template<pixel_surface Surface>
    requires std::same_as<typename Surface::pixel_type, bool>
void draw_text(
    Surface& surface,
    const font::proportional_font& font,
    int x,
    int y,
    std::string_view text
) noexcept {
    draw_text(surface, font, x, y, text, true);
}

/**
 * @headerfile <idfxx/gfx>
 * @brief Horizontal alignment of lines within a text box (see @ref draw_text_box).
 */
enum class text_align : uint8_t {
    left,   ///< Lines start at the box's left edge.
    center, ///< Lines are centered between the box's edges.
    right,  ///< Lines end at the box's right edge.
};

/**
 * @cond INTERNAL
 * @brief Returns the left edge of a line of @p line_width aligned within a box.
 */
namespace detail {
// Offsets of text box lines from the box's corner are capped at this value.
// A line this far past an int position is beyond every drawable rectangle,
// and so is any ink that its glyphs overhang.
inline constexpr uint64_t far_offset = uint64_t{1} << 40;

[[nodiscard, gnu::noinline]] inline wide
aligned_x(int x, size_t width, size_t line_width, text_align align) noexcept {
    if (line_width >= width) {
        return x;
    }
    switch (align) {
    case text_align::center:
        return wide{x} +
            static_cast<wide>(std::min<uint64_t>((width - line_width) / 2, far_offset));
    case text_align::right:
        return wide{x} + static_cast<wide>(std::min<uint64_t>(width - line_width, far_offset));
    default:
        return x;
    }
}

// Returns the top edge of line `index` of a text box whose first line's top is at y.
[[nodiscard, gnu::noinline]] inline wide
line_top(int y, size_t index, size_t line_height) noexcept {
    uint64_t offset = 0;
    if (__builtin_mul_overflow(uint64_t{index}, uint64_t{line_height}, &offset)) {
        offset = far_offset;
    }
    return wide{y} + static_cast<wide>(std::min(offset, far_offset));
}

// Returns the pen position of the hyphen ending a line of line_width
// starting at left.
[[nodiscard, gnu::noinline]] inline wide
hyphen_x(wide left, size_t line_width, const font::proportional_font& font) noexcept {
    return left + static_cast<wide>(std::min<uint64_t>(line_width, far_offset)) -
        static_cast<wide>(font.advance(U'-'));
}
} // namespace detail
/** @endcond */

/**
 * @brief Draws text in a proportional font wrapped to a box, with the given ink.
 *
 * Breaks @p text into lines no wider than @p width with
 * @ref font::break_lines. It wraps at spaces and after hyphens, hyphenates
 * at soft hyphens (U+00AD), breaks lines at `'\n'`, and splits words that
 * are too long for a line. Then it draws each line as @ref draw_text does,
 * one `font.line_height` below the previous one, starting with the first
 * line's top edge at @p y. Each line is aligned within the columns
 * [@p x, @p x + @p width) by @p align. A line wider than the box (a single
 * character that does not fit) starts at @p x. A line broken at a soft
 * hyphen ends with a drawn `'-'`.
 *
 * The box limits only the line width. All lines are drawn, however many
 * there are. The return value gives the height of the lines, so that the
 * caller can place the content that follows. As for @ref draw_text, only
 * ink pixels are written, and pixels outside the surface are clipped.
 *
 * @code
 * int y = 8;
 * y += idfxx::gfx::draw_text_box(fb, idfxx::font::inter_16, 8, y, fb.width() - 16,
 *                                "Fresh pastries, baked every morning.", white,
 *                                idfxx::gfx::text_align::center);
 * @endcode
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the box, in pixels; may be negative or off the surface.
 * @param y       Top edge of the first line, in pixels; may be negative or off the surface.
 * @param width   Width of the box, in pixels.
 * @param text    The UTF-8 text to draw.
 * @param ink     The pixel value to write for glyph ink.
 * @param align   Horizontal alignment of each line within the box.
 * @return The height of the drawn text in pixels: the number of lines times
 *         `font.line_height` (0 for empty text).
 */
template<pixel_surface Surface>
size_t draw_text_box(
    Surface& surface,
    const font::proportional_font& font,
    int x,
    int y,
    size_t width,
    std::string_view text,
    typename Surface::pixel_type ink,
    text_align align = text_align::left
) noexcept {
    size_t lines = 0;
    for (const font::text_line& line : font::break_lines(font, text, width)) {
        const detail::wide left = detail::aligned_x(x, width, line.width, align);
        const detail::wide top = detail::line_top(y, lines, font.line_height);
        detail::draw_proportional(surface, font, left, top, line.text, ink);
        if (line.hyphenated) {
            detail::draw_proportional(
                surface, font, detail::hyphen_x(left, line.width, font), top, "-", ink
            );
        }
        ++lines;
    }
    return lines * font.line_height;
}

/**
 * @brief Draws text in a proportional font wrapped to a box on a monochrome surface, setting
 *        glyph ink pixels.
 *
 * Equivalent to @ref draw_text_box with `ink = true`. See that function for
 * the full layout and rendering contract.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface with a
 *                 `bool` pixel type).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the box, in pixels; may be negative or off the surface.
 * @param y       Top edge of the first line, in pixels; may be negative or off the surface.
 * @param width   Width of the box, in pixels.
 * @param text    The UTF-8 text to draw.
 * @param align   Horizontal alignment of each line within the box.
 * @return The height of the drawn text in pixels: the number of lines times
 *         `font.line_height` (0 for empty text).
 */
template<pixel_surface Surface>
    requires std::same_as<typename Surface::pixel_type, bool>
size_t draw_text_box(
    Surface& surface,
    const font::proportional_font& font,
    int x,
    int y,
    size_t width,
    std::string_view text,
    text_align align = text_align::left
) noexcept {
    return draw_text_box(surface, font, x, y, width, text, true, align);
}

/**
 * @brief Draws text in a fixed-cell font wrapped to a box, with the given ink.
 *
 * Breaks @p text into lines no wider than @p width with
 * @ref font::break_lines. It wraps at spaces and after hyphens, breaks lines
 * at `'\n'`, and splits words that are too long for a line. Then it draws
 * each line as @ref draw_text does, `font.height * scale` rows below the
 * previous one, starting with the first line's top edge at @p y. Each line
 * is aligned within the columns [@p x, @p x + @p width) by @p align. A line
 * wider than the box (a single character that does not fit) starts at
 * @p x.
 *
 * The box limits only the line width. All lines are drawn, however many
 * there are. The return value gives the height of the lines, so that the
 * caller can place the content that follows. As for @ref draw_text, only
 * ink pixels are written, and pixels outside the surface are clipped.
 *
 * @code
 * idfxx::gfx::draw_text_box(fb, idfxx::font::spleen_5x8, 0, 16, fb.width(),
 *                           "Battery low - plug in the charger", true,
 *                           idfxx::gfx::text_align::center);
 * @endcode
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the box, in pixels; may be negative or off the surface.
 * @param y       Top edge of the first line, in pixels; may be negative or off the surface.
 * @param width   Width of the box, in pixels.
 * @param text    The text to draw.
 * @param ink     The pixel value to write for glyph ink.
 * @param align   Horizontal alignment of each line within the box.
 * @param scale   Integer magnification factor (>= 1; 0 is treated as 1).
 *                Each font pixel becomes a scale x scale block.
 * @return The height of the drawn text in pixels: the number of lines times
 *         `font.height * scale` (0 for empty text).
 */
template<pixel_surface Surface>
size_t draw_text_box(
    Surface& surface,
    const font::mono_font& font,
    int x,
    int y,
    size_t width,
    std::string_view text,
    typename Surface::pixel_type ink,
    text_align align = text_align::left,
    unsigned scale = 1
) noexcept {
    if (scale == 0) {
        scale = 1;
    }
    const size_t line_height = size_t{font.height} * scale;
    size_t lines = 0;
    for (const font::text_line& line : font::break_lines(font, text, width, scale)) {
        detail::draw_mono(
            surface,
            font,
            detail::aligned_x(x, width, line.width, align),
            detail::line_top(y, lines, line_height),
            line.text,
            ink,
            scale
        );
        ++lines;
    }
    return lines * line_height;
}

/**
 * @brief Draws text in a fixed-cell font wrapped to a box on a monochrome surface, setting glyph
 *        ink pixels.
 *
 * Equivalent to @ref draw_text_box with `ink = true`. See that function for
 * the full layout and rendering contract.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface with a
 *                 `bool` pixel type).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the box, in pixels; may be negative or off the surface.
 * @param y       Top edge of the first line, in pixels; may be negative or off the surface.
 * @param width   Width of the box, in pixels.
 * @param text    The text to draw.
 * @param align   Horizontal alignment of each line within the box.
 * @return The height of the drawn text in pixels: the number of lines times
 *         `font.height` (0 for empty text).
 */
template<pixel_surface Surface>
    requires std::same_as<typename Surface::pixel_type, bool>
size_t draw_text_box(
    Surface& surface,
    const font::mono_font& font,
    int x,
    int y,
    size_t width,
    std::string_view text,
    text_align align = text_align::left
) noexcept {
    return draw_text_box(surface, font, x, y, width, text, true, align);
}

/**
 * @headerfile <idfxx/gfx>
 * @brief A drawing view that combines a pixel surface with the drawing primitives.
 *
 * A canvas holds a reference to a surface and provides the drawing
 * primitives as members. So a sequence of drawing calls is a set of
 * operations on one object, not a set of free functions that each take the
 * surface:
 *
 * @code
 * idfxx::lcd::rgb565_framebuffer band(240, 40);
 * idfxx::gfx::canvas canvas(band);
 *
 * canvas.clear();
 * canvas.fill_rect(8, 8, 60, 24, red);
 * canvas.draw_rect(6, 6, 64, 28, white);
 * canvas.draw_text(idfxx::font::spleen_8x16, 84, 12, "red", red);
 * panel.write(band, 0, 40);
 * @endcode
 *
 * Each drawing member is equivalent to the free function of the same name
 * applied to the underlying surface, with the same ink-only rendering and
 * clipping contract. @ref fill and @ref clear use the surface's own
 * `fill`/`clear` when it provides them (framebuffers do, and fill their
 * backing store directly). Otherwise @ref fill and @ref clear use per-pixel
 * writes. A canvas only draws. To show the image on screen, write the
 * surface that the canvas draws on to the display, for example
 * `panel.write(band, 0, 40)`.
 *
 * A canvas can also place its surface within a larger drawing-coordinate
 * space. A canvas constructed with an origin and the space's size, as in
 * `canvas(band, 0, y, DISPLAY_W, DISPLAY_H)`, places the surface's top-left
 * corner at (0, y) in a DISPLAY_W x DISPLAY_H space. So a frame taller than
 * the surface can be drawn once in full-screen coordinates and rendered
 * band by band. Everything outside the band is clipped on all four sides.
 * For glyphs and lines that cross a band edge, exactly their visible part
 * is drawn. @ref width / @ref height report the full screen in every band:
 *
 * @code
 * idfxx::lcd::rgb565_framebuffer band(DISPLAY_W, BAND_H);
 * for (size_t y = 0; y < DISPLAY_H; y += BAND_H) {
 *     idfxx::gfx::canvas canvas(band, 0, y, DISPLAY_W, DISPLAY_H);
 *     canvas.clear();
 *     draw_frame(canvas); // draws in full-screen coordinates
 *     panel.write(band, 0, y);
 * }
 * @endcode
 *
 * @ref render_banded implements this loop. Use it instead of writing the
 * loop by hand.
 *
 * The inverse mapping is @ref window. It returns a canvas for a sub-region
 * of this one, with its own local coordinates and clipping. For example, a
 * widget can receive a canvas where (0, 0) is the widget's corner.
 *
 * A canvas is a cheap, copyable view. It does not own the surface, and the
 * caller must ensure that the surface outlives it. A canvas itself satisfies
 * @ref pixel_surface, so it can be passed anywhere a surface is expected.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 */
template<pixel_surface Surface>
class canvas {
public:
    /** @brief The pixel value type of the underlying surface. */
    using pixel_type = typename Surface::pixel_type;

    /**
     * @brief Creates a canvas drawing on the given surface.
     *
     * The canvas's coordinates are the same as the surface's. (0, 0) is the
     * surface's top-left corner. @ref width / @ref height match the
     * surface's dimensions (captured at construction), each clamped to
     * `INT_MAX`.
     *
     * @param surface The surface to draw on; must outlive the canvas.
     */
    explicit canvas(Surface& surface) noexcept
        : _surface(&surface)
        , _dx(0)
        , _dy(0)
        , _left(0)
        , _top(0)
        , _width(detail::limit_extent(surface.width()))
        , _height(detail::limit_extent(surface.height())) {}

    /**
     * @brief Creates a canvas for a @p width x @p height drawing space whose surface is at
     *        (@p x, @p y) in it.
     *
     * Declares that the surface holds the region of the drawing space whose
     * top-left corner is (@p x, @p y). This is the mapping for band
     * rendering. The space's size is given so that @ref width and
     * @ref height report the whole space (the full frame) wherever the
     * surface is placed. Drawing uses the space's coordinates. Pixels outside
     * the surface, or outside the space, are clipped on all four sides. A @p width or @p height
     * past `INT_MAX` is clamped to `INT_MAX`, so the space never includes
     * column or row `INT_MAX`.
     *
     * @code
     * idfxx::lcd::rgb565_framebuffer band(240, 40);
     * idfxx::gfx::canvas canvas(band, 0, 80, 240, 320); // rows 80-119 of a 240x320 frame
     * canvas.draw_text(idfxx::font::spleen_8x16, 8, canvas.height() - 20, "footer", white);
     * @endcode
     *
     * @param surface The surface to draw on; must outlive the canvas.
     * @param x       Canvas-coordinate column of the surface's left edge; may be negative (the
     *                surface's columns left of the space are clipped).
     * @param y       Canvas-coordinate row of the surface's top edge; may be negative (the
     *                surface's rows above the space are clipped).
     * @param width   Width of the drawing space, in pixels.
     * @param height  Height of the drawing space, in pixels.
     */
    canvas(Surface& surface, int x, int y, size_t width, size_t height) noexcept
        : _surface(&surface)
        , _dx(x)
        , _dy(y)
        , _left(0)
        , _top(0)
        , _width(detail::limit_extent(width))
        , _height(detail::limit_extent(height)) {}

    /** @brief Returns the underlying surface. */
    [[nodiscard]] Surface& surface() const noexcept { return *_surface; }

    /**
     * @brief Returns the width of the canvas's coordinate space, in pixels.
     *
     * Coordinates at or beyond this bound are clipped. For a canvas that
     * covers its whole surface, this is the surface width. For a translated
     * canvas, it is the drawing-space width that it was constructed with.
     * For a @ref window, it is the window width. It is at most `INT_MAX`.
     */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /**
     * @brief Returns the height of the canvas's coordinate space, in pixels.
     *
     * Coordinates at or beyond this bound are clipped. For a canvas that
     * covers its whole surface, this is the surface height. For a translated
     * canvas, it is the drawing-space height that it was constructed with.
     * For a @ref window, it is the window height. It is at most `INT_MAX`.
     */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Returns a canvas for a sub-region of this one.
     *
     * The returned canvas has its own local coordinates. (0, 0) is the
     * sub-region's top-left corner, which is at (@p x, @p y) on this canvas.
     * The returned canvas clips to the sub-region's bounds, so drawing stays
     * inside the sub-region. This includes @ref fill and @ref clear, which
     * affect only the sub-region. The sub-region can extend past any edge of
     * this canvas. @p x and @p y can be negative, for example for a widget
     * that slides in from the left. The part outside this canvas is clipped.
     * The sub-region's far edges are clamped to this canvas's bounds, so
     * @ref width and @ref height report at most the space left on this
     * canvas. The far edges are also clamped to `INT_MAX` in the
     * sub-region's own coordinates, so the part of this canvas at local
     * column or row `INT_MAX` or beyond is also clipped. An empty result is
     * allowed and draws nothing.
     *
     * @code
     * auto gauge = canvas.window(50, 60, 100, 30);
     * gauge.clear();
     * gauge.draw_text(idfxx::font::spleen_5x8, 2, 2, "RPM", white);
     * @endcode
     *
     * @param x      Column of the sub-region's left edge, in this canvas's coordinates; may be
     *               negative or off the canvas.
     * @param y      Row of the sub-region's top edge, in this canvas's coordinates; may be
     *               negative or off the canvas.
     * @param width  Width of the sub-region, in pixels.
     * @param height Height of the sub-region, in pixels.
     * @return A canvas over the same surface, restricted to the sub-region.
     */
    [[nodiscard]] canvas window(int x, int y, size_t width, size_t height) const noexcept {
        canvas sub(*this);
        sub._dx = _dx - x;
        sub._dy = _dy - y;
        // This canvas's bounds [lo, hi) in the sub-region's coordinates,
        // intersected with [0, size).
        const detail::interval cols = detail::window_bounds(x, width, _left, _width);
        const detail::interval rows = detail::window_bounds(y, height, _top, _height);
        sub._left = cols.begin;
        sub._width = cols.end;
        sub._top = rows.begin;
        sub._height = rows.end;
        return sub;
    }

    /**
     * @brief Sets a single pixel to the given ink.
     *
     * Coordinates outside the canvas bounds, or outside the underlying
     * surface, are ignored.
     *
     * @param x   Column, in `[0, width())`.
     * @param y   Row, in `[0, height())`.
     * @param ink The pixel value to write.
     */
    void set_pixel(size_t x, size_t y, pixel_type ink) noexcept {
        if (x < _left || x >= _width || y < _top || y >= _height) {
            return;
        }
        // Coordinates left of or above the surface wrap to huge values and
        // are ignored by the surface's own bounds check.
        _surface->set_pixel(x - static_cast<size_t>(_dx), y - static_cast<size_t>(_dy), ink);
    }

    /**
     * @brief Sets every drawable pixel to the given ink.
     *
     * Fills the canvas's drawable region, which is the part of its
     * coordinate space that is backed by the surface. When the surface
     * provides its own `fill` and the region covers the whole surface (the
     * identity and whole-band cases), this uses the surface's `fill`.
     * Otherwise it fills the region rectangle.
     *
     * @param ink The pixel value to fill with.
     */
    void fill(pixel_type ink) noexcept {
        if constexpr (requires(Surface& s, pixel_type i) {
                          { s.fill(i) } noexcept;
                      }) {
            if (_covers_surface()) {
                _surface->fill(ink);
                return;
            }
        }
        fill_rect(0, 0, _width, _height, ink);
    }

    /**
     * @brief Sets every drawable pixel to the default value (equivalent to `fill(pixel_type{})`).
     *
     * Clears the canvas's drawable region, which is the part of its
     * coordinate space that is backed by the surface. When the surface
     * provides its own `clear` and the region covers the whole surface (the
     * identity and whole-band cases), this uses the surface's `clear`.
     * Otherwise it fills the region with a value-initialized pixel (false on
     * monochrome surfaces, black on RGB565 ones).
     */
    void clear() noexcept {
        if constexpr (requires(Surface& s) {
                          { s.clear() } noexcept;
                      }) {
            if (_covers_surface()) {
                _surface->clear();
                return;
            }
        }
        fill(pixel_type{});
    }

    /**
     * @brief Fills a rectangle with the given ink.
     *
     * The rectangle's top-left corner is at (@p x, @p y) and it spans
     * @p width columns and @p height rows. Any part falling outside the
     * canvas is clipped.
     *
     * @param x      Left edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param y      Top edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param width  Width of the rectangle, in pixels.
     * @param height Height of the rectangle, in pixels.
     * @param ink    The pixel value to write.
     */
    void fill_rect(int x, int y, size_t width, size_t height, pixel_type ink) noexcept {
        // Clip to the drawable region in canvas coordinates, then fill the
        // translated rectangle on the surface directly. The drawable region
        // is inside the surface's own drawable rectangle, so every write is on
        // the surface.
        const detail::clip_rect r = gfx_clip_rect(*this).intersect(x, y, width, height);
        const size_t left = r.left - static_cast<size_t>(_dx);
        const size_t top = r.top - static_cast<size_t>(_dy);
        for (size_t py = top; py < top + (r.bottom - r.top); ++py) {
            for (size_t px = left; px < left + (r.right - r.left); ++px) {
                _surface->set_pixel(px, py, ink);
            }
        }
    }

    /**
     * @brief Draws a horizontal line with the given ink.
     *
     * The line starts at (@p x, @p y) and extends @p length pixels to the
     * right. Any part falling outside the canvas is clipped.
     *
     * @param x      Column of the line's left end, in pixels; may be negative or off the canvas.
     * @param y      Row of the line, in pixels; may be negative or off the canvas.
     * @param length Length of the line, in pixels.
     * @param ink    The pixel value to write.
     */
    void draw_hline(int x, int y, size_t length, pixel_type ink) noexcept {
        fill_rect(x, y, length, 1, ink);
    }

    /**
     * @brief Draws a vertical line with the given ink.
     *
     * The line starts at (@p x, @p y) and extends @p length pixels downward.
     * Any part falling outside the canvas is clipped.
     *
     * @param x      Column of the line, in pixels; may be negative or off the canvas.
     * @param y      Row of the line's top end, in pixels; may be negative or off the canvas.
     * @param length Length of the line, in pixels.
     * @param ink    The pixel value to write.
     */
    void draw_vline(int x, int y, size_t length, pixel_type ink) noexcept {
        fill_rect(x, y, 1, length, ink);
    }

    /**
     * @brief Outlines a rectangle with the given ink.
     *
     * The rectangle's top-left corner is at (@p x, @p y) and it spans
     * @p width columns and @p height rows. Only its one-pixel border is
     * drawn. Any part falling outside the canvas is clipped.
     *
     * @param x      Left edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param y      Top edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param width  Width of the rectangle, in pixels.
     * @param height Height of the rectangle, in pixels.
     * @param ink    The pixel value to write.
     */
    void draw_rect(int x, int y, size_t width, size_t height, pixel_type ink) noexcept {
        detail::outline_rect(x, y, width, height, [&](int rx, int ry, size_t rw, size_t rh) {
            fill_rect(rx, ry, rw, rh, ink);
        });
    }

    /**
     * @brief Draws a straight line between two points with the given ink.
     *
     * Both endpoints are inclusive. Any part falling outside the canvas is
     * clipped.
     *
     * @param x0  Column of the first endpoint, in pixels; may be negative or off the canvas.
     * @param y0  Row of the first endpoint, in pixels; may be negative or off the canvas.
     * @param x1  Column of the second endpoint, in pixels; may be negative or off the canvas.
     * @param y1  Row of the second endpoint, in pixels; may be negative or off the canvas.
     * @param ink The pixel value to write.
     */
    void draw_line(int x0, int y0, int x1, int y1, pixel_type ink) noexcept {
        gfx::draw_line(*this, x0, y0, x1, y1, ink);
    }

    /**
     * @brief Fills an ellipse with the given ink.
     *
     * Fills the ellipse inscribed in the box whose top-left corner is at
     * (@p x, @p y) and which spans @p width columns and @p height rows. A
     * pixel of the box is filled when its center is inside the ellipse or
     * on it. So the ellipse is symmetric, and boxes of even and odd sizes
     * work the same way. A zero @p width or @p height draws nothing. Any
     * part falling outside the canvas is clipped.
     *
     * @param x      Left edge of the bounding box, in pixels; may be negative or off the canvas.
     * @param y      Top edge of the bounding box, in pixels; may be negative or off the canvas.
     * @param width  Width of the bounding box, in pixels.
     * @param height Height of the bounding box, in pixels.
     * @param ink    The pixel value to write.
     */
    void fill_ellipse(int x, int y, size_t width, size_t height, pixel_type ink) noexcept {
        gfx::fill_ellipse(*this, x, y, width, height, ink);
    }

    /**
     * @brief Outlines an ellipse with the given ink.
     *
     * Draws the one-pixel boundary of the ellipse that @ref fill_ellipse
     * fills with the same arguments. The boundary is the filled pixels that
     * have a horizontal or vertical neighbor that is not filled. A zero
     * @p width or @p height draws nothing. Any part falling outside the
     * canvas is clipped.
     *
     * @param x      Left edge of the bounding box, in pixels; may be negative or off the canvas.
     * @param y      Top edge of the bounding box, in pixels; may be negative or off the canvas.
     * @param width  Width of the bounding box, in pixels.
     * @param height Height of the bounding box, in pixels.
     * @param ink    The pixel value to write.
     */
    void draw_ellipse(int x, int y, size_t width, size_t height, pixel_type ink) noexcept {
        gfx::draw_ellipse(*this, x, y, width, height, ink);
    }

    /**
     * @brief Fills a circle with the given ink.
     *
     * Fills the ellipse that @ref fill_ellipse fills in the square box of
     * `2 * radius + 1` pixels centered on the pixel (@p cx, @p cy). A
     * radius of 0 fills only the center pixel. A radius past `INT_MAX` is
     * treated as `INT_MAX`. Any part falling outside the canvas is clipped.
     *
     * @param cx     Column of the center pixel; may be negative or off the canvas.
     * @param cy     Row of the center pixel; may be negative or off the canvas.
     * @param radius Radius, in pixels, from the center pixel to the edge pixels.
     * @param ink    The pixel value to write.
     */
    void fill_circle(int cx, int cy, size_t radius, pixel_type ink) noexcept {
        gfx::fill_circle(*this, cx, cy, radius, ink);
    }

    /**
     * @brief Outlines a circle with the given ink.
     *
     * Draws the one-pixel boundary of the circle that @ref fill_circle fills
     * with the same arguments. A radius past `INT_MAX` is treated as `INT_MAX`.
     * Any part falling outside the canvas is clipped.
     *
     * @param cx     Column of the center pixel; may be negative or off the canvas.
     * @param cy     Row of the center pixel; may be negative or off the canvas.
     * @param radius Radius, in pixels, from the center pixel to the edge pixels.
     * @param ink    The pixel value to write.
     */
    void draw_circle(int cx, int cy, size_t radius, pixel_type ink) noexcept {
        gfx::draw_circle(*this, cx, cy, radius, ink);
    }

    /**
     * @brief Fills a rectangle with rounded corners with the given ink.
     *
     * The rectangle's top-left corner is at (@p x, @p y) and it spans
     * @p width columns and @p height rows. Each corner is a quadrant of the
     * ellipse that @ref fill_ellipse fills in a `2 * radius` square box. A
     * radius of 0 fills the plain rectangle. A zero @p width or @p height
     * draws nothing. Any part falling outside the canvas is clipped.
     *
     * @param x      Left edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param y      Top edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param width  Width of the rectangle, in pixels.
     * @param height Height of the rectangle, in pixels.
     * @param radius Corner radius, in pixels; clamped to `min(width, height) / 2`.
     * @param ink    The pixel value to write.
     */
    void fill_round_rect(
        int x,
        int y,
        size_t width,
        size_t height,
        size_t radius,
        pixel_type ink
    ) noexcept {
        gfx::fill_round_rect(*this, x, y, width, height, radius, ink);
    }

    /**
     * @brief Outlines a rectangle with rounded corners with the given ink.
     *
     * Draws the one-pixel boundary of the rounded rectangle that
     * @ref fill_round_rect fills with the same arguments. The boundary is
     * the filled pixels that have a horizontal or vertical neighbor that is
     * not filled. A zero @p width or @p height draws nothing. Any part
     * falling outside the canvas is clipped.
     *
     * @param x      Left edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param y      Top edge of the rectangle, in pixels; may be negative or off the canvas.
     * @param width  Width of the rectangle, in pixels.
     * @param height Height of the rectangle, in pixels.
     * @param radius Corner radius, in pixels; clamped to `min(width, height) / 2`.
     * @param ink    The pixel value to write.
     */
    void draw_round_rect(
        int x,
        int y,
        size_t width,
        size_t height,
        size_t radius,
        pixel_type ink
    ) noexcept {
        gfx::draw_round_rect(*this, x, y, width, height, radius, ink);
    }

    /**
     * @brief Fills a triangle with the given ink.
     *
     * Each vertex names a pixel, as a @ref draw_line endpoint does. A pixel
     * is filled when its center is inside the triangle or on one of its
     * edges. Every pixel that @ref draw_triangle draws for the same vertices,
     * in any order, is also filled. So a filled triangle lines up with its
     * border, and triangles that share an edge have no gap between them. Any
     * part falling outside the canvas is clipped.
     *
     * @param x0  Column of the first vertex, in pixels; may be negative or off the canvas.
     * @param y0  Row of the first vertex, in pixels; may be negative or off the canvas.
     * @param x1  Column of the second vertex, in pixels; may be negative or off the canvas.
     * @param y1  Row of the second vertex, in pixels; may be negative or off the canvas.
     * @param x2  Column of the third vertex, in pixels; may be negative or off the canvas.
     * @param y2  Row of the third vertex, in pixels; may be negative or off the canvas.
     * @param ink The pixel value to write.
     */
    void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, pixel_type ink) noexcept {
        gfx::fill_triangle(*this, x0, y0, x1, y1, x2, y2, ink);
    }

    /**
     * @brief Outlines a triangle with the given ink.
     *
     * Draws each of the triangle's three edges as @ref draw_line does. Both
     * endpoints are drawn. Each edge is drawn from its upper endpoint (the
     * left one for a horizontal edge) to the other endpoint, so the pixels
     * drawn do not depend on the order in which the vertices are given.
     * @ref fill_triangle also fills these pixels for the same vertices, so a
     * filled triangle and its outline line up. Any part falling outside the
     * canvas is clipped.
     *
     * @param x0  Column of the first vertex, in pixels; may be negative or off the canvas.
     * @param y0  Row of the first vertex, in pixels; may be negative or off the canvas.
     * @param x1  Column of the second vertex, in pixels; may be negative or off the canvas.
     * @param y1  Row of the second vertex, in pixels; may be negative or off the canvas.
     * @param x2  Column of the third vertex, in pixels; may be negative or off the canvas.
     * @param y2  Row of the third vertex, in pixels; may be negative or off the canvas.
     * @param ink The pixel value to write.
     */
    void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, pixel_type ink) noexcept {
        gfx::draw_triangle(*this, x0, y0, x1, y1, x2, y2, ink);
    }

    /**
     * @brief Draws a 1-bit bitmap's set pixels with the given ink.
     *
     * Places the bitmap's top-left pixel at (@p x, @p y) and writes @p ink
     * where its bits are set. The pixels under its clear bits are not
     * changed, so bitmaps drawn at one origin in different inks appear as
     * layers.
     * Any part falling outside the canvas is clipped.
     *
     * @param x      Column of the bitmap's left edge, in pixels; may be negative or off the canvas.
     * @param y      Row of the bitmap's top edge, in pixels; may be negative or off the canvas.
     * @param bitmap The bitmap to draw; only the complete rows that its data holds are drawn.
     * @param ink    The pixel value to write for set bits.
     */
    void draw_bitmap(int x, int y, const mono_bitmap& bitmap, pixel_type ink) noexcept {
        gfx::draw_bitmap(*this, x, y, bitmap, ink);
    }

    /**
     * @brief Draws a 1-bit bitmap opaquely, with set pixels in one value and clear pixels in
     *        another.
     *
     * Places the bitmap's top-left pixel at (@p x, @p y) and writes every
     * pixel that it covers: @p ink where its bits are set and @p paper where
     * they are clear. Any part falling outside the canvas is clipped.
     *
     * @param x      Column of the bitmap's left edge, in pixels; may be negative or off the canvas.
     * @param y      Row of the bitmap's top edge, in pixels; may be negative or off the canvas.
     * @param bitmap The bitmap to draw; only the complete rows that its data holds are drawn.
     * @param ink    The pixel value to write for set bits.
     * @param paper  The pixel value to write for clear bits.
     */
    void draw_bitmap(
        int x,
        int y,
        const mono_bitmap& bitmap,
        pixel_type ink,
        pixel_type paper
    ) noexcept {
        gfx::draw_bitmap(*this, x, y, bitmap, ink, paper);
    }

    /**
     * @brief Draws text with the given ink.
     *
     * Draws @p text left to right, with its top-left corner at
     * (@p x, @p y). Only the glyphs' ink pixels are written. Background
     * pixels within the cell are not changed, so text is drawn on top of the
     * existing content. On a monochrome surface, pass `ink = false` to erase
     * ink pixels instead, for example for inverse text on a filled banner.
     * Characters outside the font's range advance the cursor without
     * drawing. Pixels falling outside the canvas are clipped.
     *
     * @param font  Font to render with.
     * @param x     Left edge of the first glyph cell, in pixels; may be negative or off the canvas.
     * @param y     Top edge of the glyph cells, in pixels; may be negative or off the canvas.
     * @param text  The text to draw.
     * @param ink   The pixel value to write for glyph ink.
     * @param scale Integer magnification factor (>= 1; 0 is treated as 1).
     *              Each font pixel becomes a scale x scale block.
     */
    void draw_text(
        const font::mono_font& font,
        int x,
        int y,
        std::string_view text,
        pixel_type ink,
        unsigned scale = 1
    ) noexcept {
        gfx::draw_text(*this, font, x, y, text, ink, scale);
    }

    /**
     * @brief Draws text on a monochrome surface, setting glyph ink pixels.
     *
     * Equivalent to @ref draw_text with `ink = true`. See that function for
     * the full rendering contract.
     *
     * @param font Font to render with.
     * @param x    Left edge of the first glyph cell, in pixels; may be negative or off the canvas.
     * @param y    Top edge of the glyph cells, in pixels; may be negative or off the canvas.
     * @param text The text to draw.
     */
    void draw_text(const font::mono_font& font, int x, int y, std::string_view text) noexcept
        requires std::same_as<pixel_type, bool>
    {
        gfx::draw_text(*this, font, x, y, text);
    }

    /**
     * @brief Draws text in a proportional font with the given ink.
     *
     * Decodes @p text as UTF-8 and draws it as one line, left to right, with
     * the line's top-left corner at (@p x, @p y). The baseline is
     * `font.ascent` rows below @p y. Each glyph moves the pen right by its
     * own advance, so @ref font::text_width gives the width to use when
     * centering or aligning the text. Only the glyphs' ink pixels are
     * written. The background is not changed. On a monochrome surface, pass
     * `ink = false` to erase ink pixels instead. Code points that the font
     * does not have are drawn as its replacement glyph, or skipped if it has
     * none. U+00AD SOFT HYPHEN is never drawn and takes no space. Control
     * characters (including newlines) are not interpreted. See
     * @ref draw_text_box for wrapped, multi-line text. Pixels falling outside
     * the canvas are clipped.
     *
     * @param font Font to render with.
     * @param x    Left edge of the line (the starting pen position), in pixels; may be negative or
     * off the canvas.
     * @param y    Top edge of the line, in pixels; may be negative or off the canvas.
     * @param text The UTF-8 text to draw.
     * @param ink  The pixel value to write for glyph ink.
     */
    void draw_text(
        const font::proportional_font& font,
        int x,
        int y,
        std::string_view text,
        pixel_type ink
    ) noexcept {
        gfx::draw_text(*this, font, x, y, text, ink);
    }

    /**
     * @brief Draws text in a proportional font on a monochrome surface, setting glyph ink pixels.
     *
     * Equivalent to @ref draw_text with `ink = true`. See that function for
     * the full rendering contract.
     *
     * @param font Font to render with.
     * @param x    Left edge of the line (the starting pen position), in pixels; may be negative or
     * off the canvas.
     * @param y    Top edge of the line, in pixels; may be negative or off the canvas.
     * @param text The UTF-8 text to draw.
     */
    void
    draw_text(const font::proportional_font& font, int x, int y, std::string_view text) noexcept
        requires std::same_as<pixel_type, bool>
    {
        gfx::draw_text(*this, font, x, y, text);
    }

    /**
     * @brief Draws text in a proportional font wrapped to a box, with the given ink.
     *
     * Breaks @p text into lines no wider than @p width with
     * @ref font::break_lines. It wraps at spaces and after hyphens,
     * hyphenates at soft hyphens (U+00AD), breaks lines at `'\n'`, and
     * splits words that are too long for a line. Then it draws each line as
     * @ref draw_text does, one `font.line_height` below the previous one,
     * starting with the first line's top edge at @p y. Each line is aligned
     * within the columns [@p x, @p x + @p width) by @p align. A line wider
     * than the box (a single character that does not fit) starts at @p x. A
     * line broken at a soft hyphen ends with a drawn `'-'`. All lines are
     * drawn, however many there are. Pixels falling outside the canvas are
     * clipped.
     *
     * @code
     * int y = 8;
     * y += canvas.draw_text_box(idfxx::font::inter_16, 8, y, canvas.width() - 16,
     *                           "Fresh pastries, baked every morning.", white,
     *                           idfxx::gfx::text_align::center);
     * @endcode
     *
     * @param font  Font to render with.
     * @param x     Left edge of the box, in pixels; may be negative or off the canvas.
     * @param y     Top edge of the first line, in pixels; may be negative or off the canvas.
     * @param width Width of the box, in pixels.
     * @param text  The UTF-8 text to draw.
     * @param ink   The pixel value to write for glyph ink.
     * @param align Horizontal alignment of each line within the box.
     * @return The height of the drawn text in pixels: the number of lines
     *         times `font.line_height` (0 for empty text).
     */
    size_t draw_text_box(
        const font::proportional_font& font,
        int x,
        int y,
        size_t width,
        std::string_view text,
        pixel_type ink,
        text_align align = text_align::left
    ) noexcept {
        return gfx::draw_text_box(*this, font, x, y, width, text, ink, align);
    }

    /**
     * @brief Draws text in a proportional font wrapped to a box on a monochrome surface, setting
     *        glyph ink pixels.
     *
     * Equivalent to @ref draw_text_box with `ink = true`. See that function
     * for the full layout and rendering contract.
     *
     * @param font  Font to render with.
     * @param x     Left edge of the box, in pixels; may be negative or off the canvas.
     * @param y     Top edge of the first line, in pixels; may be negative or off the canvas.
     * @param width Width of the box, in pixels.
     * @param text  The UTF-8 text to draw.
     * @param align Horizontal alignment of each line within the box.
     * @return The height of the drawn text in pixels: the number of lines
     *         times `font.line_height` (0 for empty text).
     */
    size_t draw_text_box(
        const font::proportional_font& font,
        int x,
        int y,
        size_t width,
        std::string_view text,
        text_align align = text_align::left
    ) noexcept
        requires std::same_as<pixel_type, bool>
    {
        return gfx::draw_text_box(*this, font, x, y, width, text, align);
    }

    /**
     * @brief Draws text in a fixed-cell font wrapped to a box, with the given ink.
     *
     * Breaks @p text into lines no wider than @p width with
     * @ref font::break_lines. It wraps at spaces and after hyphens, breaks
     * lines at `'\n'`, and splits words that are too long for a line. Then
     * it draws each line as @ref draw_text does, `font.height * scale` rows
     * below the previous one, starting with the first line's top edge at
     * @p y. Each line is aligned within the columns [@p x, @p x + @p width)
     * by @p align. A line wider than the box (a single character that does
     * not fit) starts at @p x. All lines are drawn, however many there are.
     * Pixels falling outside the canvas are clipped.
     *
     * @param font  Font to render with.
     * @param x     Left edge of the box, in pixels; may be negative or off the canvas.
     * @param y     Top edge of the first line, in pixels; may be negative or off the canvas.
     * @param width Width of the box, in pixels.
     * @param text  The text to draw.
     * @param ink   The pixel value to write for glyph ink.
     * @param align Horizontal alignment of each line within the box.
     * @param scale Integer magnification factor (>= 1; 0 is treated as 1).
     *              Each font pixel becomes a scale x scale block.
     * @return The height of the drawn text in pixels: the number of lines
     *         times `font.height * scale` (0 for empty text).
     */
    size_t draw_text_box(
        const font::mono_font& font,
        int x,
        int y,
        size_t width,
        std::string_view text,
        pixel_type ink,
        text_align align = text_align::left,
        unsigned scale = 1
    ) noexcept {
        return gfx::draw_text_box(*this, font, x, y, width, text, ink, align, scale);
    }

    /**
     * @brief Draws text in a fixed-cell font wrapped to a box on a monochrome surface, setting
     *        glyph ink pixels.
     *
     * Equivalent to @ref draw_text_box with `ink = true`. See that function
     * for the full layout and rendering contract.
     *
     * @param font  Font to render with.
     * @param x     Left edge of the box, in pixels; may be negative or off the canvas.
     * @param y     Top edge of the first line, in pixels; may be negative or off the canvas.
     * @param width Width of the box, in pixels.
     * @param text  The text to draw.
     * @param align Horizontal alignment of each line within the box.
     * @return The height of the drawn text in pixels: the number of lines
     *         times `font.height` (0 for empty text).
     */
    size_t draw_text_box(
        const font::mono_font& font,
        int x,
        int y,
        size_t width,
        std::string_view text,
        text_align align = text_align::left
    ) noexcept
        requires std::same_as<pixel_type, bool>
    {
        return gfx::draw_text_box(*this, font, x, y, width, text, align);
    }

    /**
     * @cond INTERNAL
     * @brief Returns the drawable rectangle in canvas coordinates: the
     * canvas bounds intersected with the surface's own drawable rectangle
     * placed at (_dx, _dy). Found by detail::clip_of through
     * argument-dependent lookup.
     */
    [[nodiscard]] friend detail::clip_rect gfx_clip_rect(const canvas& c) noexcept {
        return detail::translate_clip(
            detail::clip_of(*c._surface), c._dx, c._dy, c._left, c._top, c._width, c._height
        );
    }
    /** @endcond */

private:
    // True when the drawable region spans the entire surface (the identity
    // and whole-band cases). In these cases fill and clear can use the
    // surface's native fill/clear.
    [[nodiscard]] bool _covers_surface() const noexcept {
        return detail::covers(
            _dx, _dy, _surface->width(), _surface->height(), _left, _top, _width, _height
        );
    }

    Surface* _surface;
    detail::wide _dx; // canvas-coordinate position of the surface's (0, 0)
    detail::wide _dy;
    size_t _left; // canvas-coordinate clip bounds: [_left, _width) x [_top, _height)
    size_t _top;
    size_t _width;
    size_t _height;
};

/**
 * @headerfile <idfxx/gfx>
 * @brief Mounting direction for @ref rotated.
 *
 * Only quarter turns are supported. These are the two directions in which a
 * portrait panel can be mounted for a landscape layout (and the reverse).
 * Each value names the turn that was applied to the surface when it was
 * mounted. The adapter's remapping reverses that turn.
 */
enum class rotation : uint8_t {
    /// The surface is mounted turned a quarter turn clockwise (its native
    /// top edge at the viewer's right).
    cw90,
    /// The surface is mounted turned a quarter turn counter-clockwise (its
    /// native top edge at the viewer's left).
    ccw90,
};

/**
 * @headerfile <idfxx/gfx>
 * @brief A pixel-surface view presenting another surface turned a quarter turn.
 *
 * Adapts a surface whose native orientation differs from the drawing layout
 * by a quarter turn. This is common for ePaper: a panel that is natively
 * portrait is mounted in landscape orientation. The view swaps the reported
 * dimensions and remaps @ref set_pixel, so that drawing uses viewer
 * coordinates and the underlying surface keeps its native layout. The
 * framebuffer behind the view is written to the display in its native
 * layout (`display.write(fb)`).
 *
 * @code
 * idfxx::epaper::mono_framebuffer fb(display.width(), display.height()); // 122x250 portrait
 * idfxx::gfx::rotated view(fb, idfxx::gfx::rotation::cw90);              // 250x122 landscape
 * idfxx::gfx::canvas canvas(view);
 * canvas.draw_text(idfxx::font::spleen_8x16, 8, 8, "landscape");
 * // The framebuffer is written in its native layout, as always.
 * display.write(fb);
 * @endcode
 *
 * Like a canvas, a rotated view is a cheap, copyable view. It does not own
 * the surface, and the caller must ensure that the surface outlives it. It
 * satisfies @ref pixel_surface itself, with the same clipping contract:
 * writes outside its (swapped) bounds are ignored. @ref fill and @ref clear
 * call the surface's own `fill` and `clear` when it provides them.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 */
template<pixel_surface Surface>
class rotated {
public:
    /** @brief The pixel value type of the underlying surface. */
    using pixel_type = typename Surface::pixel_type;

    /**
     * @brief Creates a view of @p surface turned by @p direction.
     *
     * @param surface   The surface to draw on; must outlive the view.
     * @param direction How the surface is mounted relative to the viewer.
     */
    rotated(Surface& surface, rotation direction) noexcept
        : _surface(&surface)
        , _direction(direction) {}

    /** @brief Returns the underlying surface. */
    [[nodiscard]] Surface& surface() const noexcept { return *_surface; }

    /** @brief Returns the view's width, which is the surface's height. */
    [[nodiscard]] size_t width() const noexcept { return _surface->height(); }

    /** @brief Returns the view's height, which is the surface's width. */
    [[nodiscard]] size_t height() const noexcept { return _surface->width(); }

    /**
     * @brief Sets a single pixel to the given ink, in viewer coordinates.
     *
     * Coordinates outside the view's bounds are ignored.
     *
     * @param x   Column, in `[0, width())`.
     * @param y   Row, in `[0, height())`.
     * @param ink The pixel value to write.
     */
    void set_pixel(size_t x, size_t y, pixel_type ink) noexcept {
        // Out-of-range viewer coordinates wrap to huge surface coordinates
        // and are ignored by the surface's own bounds check.
        if (_direction == rotation::cw90) {
            _surface->set_pixel(y, _surface->height() - 1 - x, ink);
        } else {
            _surface->set_pixel(_surface->width() - 1 - y, x, ink);
        }
    }

    /**
     * @brief Sets every pixel to the given ink, on surfaces with their own `fill`.
     *
     * A quarter turn covers the whole surface either way, so this forwards
     * to the surface's native fill.
     *
     * @param ink The pixel value to fill with.
     */
    void fill(pixel_type ink) noexcept
        requires requires(Surface& s, pixel_type i) {
            { s.fill(i) } noexcept;
        }
    {
        _surface->fill(ink);
    }

    /**
     * @brief Sets every pixel to the default value, on surfaces with their own `clear`.
     */
    void clear() noexcept
        requires requires(Surface& s) {
            { s.clear() } noexcept;
        }
    {
        _surface->clear();
    }

    /**
     * @cond INTERNAL
     * @brief Returns the surface's drawable rectangle turned into viewer
     * coordinates. Found by detail::clip_of through argument-dependent
     * lookup.
     */
    [[nodiscard]] friend detail::clip_rect gfx_clip_rect(const rotated& r) noexcept {
        const detail::clip_rect s = detail::clip_of(*r._surface);
        if (s.empty()) {
            return {};
        }
        if (r._direction == rotation::cw90) {
            // Viewer (x, y) is surface (y, height - 1 - x).
            const size_t h = r._surface->height();
            return detail::limit_clip({h - s.bottom, s.left, h - s.top, s.right});
        }
        // Viewer (x, y) is surface (width - 1 - y, x).
        const size_t w = r._surface->width();
        return detail::limit_clip({s.top, w - s.right, s.bottom, w - s.left});
    }
    /** @endcond */

private:
    Surface* _surface;
    rotation _direction;
};

/**
 * @cond INTERNAL
 * @brief Constraint for the banded-rendering functions: the draw callback
 * accepts a canvas over the band, and the destination can write the band at
 * an offset.
 */
template<typename Surface, typename Dest, typename DrawFn>
concept banded_renderable = pixel_surface<Surface> &&
    requires(canvas<Surface>& c, const Surface& band, Dest& dest, DrawFn& draw) {
        draw(c);
        { dest.try_write(band, size_t{}, size_t{}) } -> std::same_as<result<void>>;
    };
/** @endcond */

/** @cond INTERNAL */
template<pixel_surface Surface, typename Dest, typename DrawFn>
    requires banded_renderable<Surface, Dest, DrawFn>
[[nodiscard]] result<void>
try_render_banded(Surface& band, Dest& dest, size_t frame_height, DrawFn&& draw);
/** @endcond */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Renders a frame taller than its framebuffer, band by band.
 *
 * Renders a @p frame_height-row frame through @p band in
 * `frame_height / band.height()` passes. @p band is a framebuffer that
 * covers `band.width()` x `band.height()` pixels. Each pass does these
 * steps:
 * - It clears the band.
 * - It calls @p draw with a canvas whose coordinates span the full frame.
 *   The canvas places the band at its slice with
 *   `canvas(band, 0, y, band.width(), frame_height)`, so the canvas's width
 *   and height are the frame's in every pass.
 * - It writes the band to @p dest at (0, y) with `dest.try_write(band, 0, y)`.
 *
 * Content outside the pass's band is clipped before drawing, not discarded
 * pixel by pixel. For content that crosses band edges, exactly its visible
 * part is drawn. So the assembled frame is identical to a frame drawn
 * through a full-frame buffer.
 *
 * The callback is called once for each band and must draw the complete
 * frame each time, as a pure function of the scene state. Compute any state
 * changes (such as advancing samples or reading sensors) before rendering,
 * not inside the callback.
 *
 * @code
 * idfxx::lcd::rgb565_framebuffer band(240, 40);
 * idfxx::gfx::render_banded(band, panel, 320, [&](auto& canvas) {
 *     canvas.draw_text(idfxx::font::spleen_8x16, 8, 8, "title", white, 2);
 *     canvas.draw_line(0, 0, 239, 319, green); // crosses every band
 * });
 * @endcode
 *
 * @tparam Surface The band's surface type (satisfies @ref pixel_surface).
 * @tparam Dest    The destination type (e.g. a panel), providing
 *                 `try_write(const Surface&, size_t x, size_t y)`.
 * @tparam DrawFn  The draw callback type, invocable with `canvas<Surface>&`.
 * @param band         The framebuffer to render through; its width is the frame width.
 * @param dest         The destination that the band is written to after each pass.
 * @param frame_height Height of the frame, in pixels; must be a non-zero
 *                     multiple of `band.height()`.
 * @param draw         Callback drawing the complete frame on the given canvas.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error (e.g. an invalid @p frame_height, or a
 *         failed write).
 */
template<pixel_surface Surface, typename Dest, typename DrawFn>
    requires banded_renderable<Surface, Dest, DrawFn>
void render_banded(Surface& band, Dest& dest, size_t frame_height, DrawFn&& draw) {
    unwrap(try_render_banded(band, dest, frame_height, draw));
}
#endif

/**
 * @brief Renders a frame taller than its framebuffer, band by band.
 *
 * Renders a @p frame_height-row frame through @p band in
 * `frame_height / band.height()` passes. @p band is a framebuffer that
 * covers `band.width()` x `band.height()` pixels. Each pass does these
 * steps:
 * - It clears the band.
 * - It calls @p draw with a canvas whose coordinates span the full frame.
 *   The canvas places the band at its slice with
 *   `canvas(band, 0, y, band.width(), frame_height)`, so the canvas's width
 *   and height are the frame's in every pass.
 * - It writes the band to @p dest at (0, y) with `dest.try_write(band, 0, y)`.
 *
 * Content outside the pass's band is clipped before drawing, not discarded
 * pixel by pixel. For content that crosses band edges, exactly its visible
 * part is drawn. So the assembled frame is identical to a frame drawn
 * through a full-frame buffer.
 *
 * The callback is called once for each band and must draw the complete
 * frame each time, as a pure function of the scene state. Compute any state
 * changes (such as advancing samples or reading sensors) before rendering,
 * not inside the callback.
 *
 * @tparam Surface The band's surface type (satisfies @ref pixel_surface).
 * @tparam Dest    The destination type (e.g. a panel), providing
 *                 `try_write(const Surface&, size_t x, size_t y)`.
 * @tparam DrawFn  The draw callback type, invocable with `canvas<Surface>&`.
 * @param band         The framebuffer to render through; its width is the frame width.
 * @param dest         The destination that the band is written to after each pass.
 * @param frame_height Height of the frame, in pixels; must be a non-zero
 *                     multiple of `band.height()`.
 * @param draw         Callback drawing the complete frame on the given canvas.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg if @p frame_height is zero or not a
 *         multiple of `band.height()`, or the band has zero height.
 */
template<pixel_surface Surface, typename Dest, typename DrawFn>
    requires banded_renderable<Surface, Dest, DrawFn>
[[nodiscard]] result<void>
try_render_banded(Surface& band, Dest& dest, size_t frame_height, DrawFn&& draw) {
    const size_t band_height = band.height();
    if (band_height == 0 || frame_height == 0 || frame_height % band_height != 0) {
        return error(errc::invalid_arg);
    }
    for (size_t y = 0; y < frame_height; y += band_height) {
        canvas c(band, 0, static_cast<int>(y), band.width(), frame_height);
        c.clear();
        draw(c);
        if (auto written = dest.try_write(std::as_const(band), size_t{0}, y); !written) {
            return written;
        }
    }
    return {};
}

} // namespace idfxx::gfx

namespace idfxx {

/**
 * @headerfile <idfxx/gfx>
 * @brief Returns a string representation of a mounting rotation.
 *
 * @param r The rotation to convert.
 * @return The enumerator name (e.g. "cw90"), or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(gfx::rotation r) {
    switch (r) {
    case gfx::rotation::cw90:
        return "cw90";
    case gfx::rotation::ccw90:
        return "ccw90";
    default:
        return "unknown(" + std::to_string(std::to_underlying(r)) + ')';
    }
}

/**
 * @headerfile <idfxx/gfx>
 * @brief Returns a string representation of a text alignment.
 *
 * @param a The alignment to convert.
 * @return The enumerator name (e.g. "center"), or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(gfx::text_align a) {
    switch (a) {
    case gfx::text_align::left:
        return "left";
    case gfx::text_align::center:
        return "center";
    case gfx::text_align::right:
        return "right";
    default:
        return "unknown(" + std::to_string(std::to_underlying(a)) + ')';
    }
}

/**
 * @headerfile <idfxx/gfx>
 * @brief Returns a string representation of a bitmap bit order.
 *
 * @param o The bit order to convert.
 * @return The enumerator name (e.g. "msb_first"), or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] inline std::string to_string(gfx::bit_order o) {
    switch (o) {
    case gfx::bit_order::msb_first:
        return "msb_first";
    case gfx::bit_order::lsb_first:
        return "lsb_first";
    default:
        return "unknown(" + std::to_string(std::to_underlying(o)) + ')';
    }
}

} // namespace idfxx

/** @} */ // end of idfxx_gfx

#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {
template<>
struct formatter<idfxx::gfx::rotation> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::gfx::rotation v, FormatContext& ctx) const {
        auto s = idfxx::to_string(v);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
template<>
struct formatter<idfxx::gfx::text_align> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::gfx::text_align v, FormatContext& ctx) const {
        auto s = idfxx::to_string(v);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
template<>
struct formatter<idfxx::gfx::bit_order> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::gfx::bit_order v, FormatContext& ctx) const {
        auto s = idfxx::to_string(v);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
