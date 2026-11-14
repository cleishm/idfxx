// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/gfx>
 * @file gfx.hpp
 * @brief Drawing primitives for pixel surfaces.
 *
 * @defgroup idfxx_gfx Graphics Component
 * @brief Rectangles, lines, and text drawing on any pixel surface.
 *
 * Provides integer drawing primitives over the @ref idfxx::gfx::pixel_surface
 * concept: any type with `set_pixel(x, y, pixel)` and reported dimensions can
 * be drawn on, with the ink value matching the surface's pixel type — `bool`
 * for monochrome framebuffers, `idfxx::lcd::rgb565` for color ones. All
 * functions render "ink only": they write the requested pixels and leave
 * everything else untouched, so drawing composes over existing content.
 *
 * Positions are signed (`int`) and sizes unsigned (`size_t`): a shape or a
 * line of text may start or extend off any edge of the surface — scrolling
 * text, a sprite sliding in from the left — and only its visible part is
 * drawn, with no clipping needed by the caller. Drawing never reaches column
 * or row `INT_MAX` or beyond: canvases and windows clamp their coordinate
 * spaces to at most `INT_MAX` columns and rows, and drawing on a surface
 * wider or taller than that clips there.
 *
 * The primitives are available two ways: as members of @ref
 * idfxx::gfx::canvas, a lightweight view bundling a surface with the drawing
 * operations, and as free functions taking the surface as their first
 * argument. @ref idfxx::gfx::rotated adapts a surface mounted a quarter turn
 * from the drawing layout (a portrait panel used landscape).
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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
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
 * ignored; drawing functions rely on this for clipping.
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
// The 64-bit arithmetic lives in the non-template helpers below, kept out of
// line, so the drawing templates instantiated per surface type run on
// size_t values that are already clipped.
using wide = std::int64_t;

// The bound every drawable rectangle stays within: clip_of clamps a plain
// surface's rectangle to [0, coordinate_limit), canvases and windows clamp
// their coordinate spaces to it, and rotated views clamp their turned
// rectangle. So a position saturated to INT_MAX is off every surface, and
// no clip coordinate is more than 2^32 - 1 from an int position.
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

// A half-open interval [begin, end); empty when begin >= end.
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
// columns or rows) starting at a signed position: the visible pixels are
// offsets [lo, hi) from the position, which lie at base + lo onward in
// size_t arithmetic (base being the position's bits, so the sum wraps back
// into range), and units [first, last) have at least one visible pixel.
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
// passes either pos >= INT_MIN or a glyph-sized run, and hi stays within
// coordinate_limit, so they are below 2^32 and fit a 32-bit size_t.
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
// `size` whose origin sits at pos on that canvas, clamped to [0, size) and
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
// coordinates that can actually change pixels; empty when right <= left or
// bottom <= top. Drawing functions skip work outside it.
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

// The visible bitmap columns and rows of a proportional glyph.
struct glyph_spans {
    unit_span cols;
    unit_span rows;
};

// Returns the visible part of glyph g's bitmap drawn with the pen at pen on
// the baseline row baseline; both spans are empty when none is visible.
[[nodiscard, gnu::noinline]] inline glyph_spans
clip_glyph(const clip_rect& clip, wide pen, wide baseline, const font::glyph_metrics& g) noexcept {
    const unit_span cols = clip_units(pen + g.x_offset, g.width, 1, clip.left, clip.right);
    if (cols.first >= cols.last) {
        return {};
    }
    return {cols, clip_units(baseline + g.y_offset, g.height, 1, clip.top, clip.bottom)};
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

// Returns the drawable rectangle of a surface: the one a surface reports
// through a gfx_clip_rect(surface) found by argument-dependent lookup (a
// canvas reports the part of its coordinate space its surface backs), or
// otherwise the whole surface; either way within coordinate_limit (the
// reported rectangles keep to it themselves).
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
 * columns and @p height rows; only its one-pixel border is drawn. Any part
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

// Returns the first step of a line walk that can draw inside a clip, for a
// line of major_len steps along its major axis and minor_len (<= major_len)
// along its minor one that reaches the clip's near edges after major_skip
// major and minor_skip minor steps. The walk's step k lies at minor step
// floor((2 k minor_len + major_len) / (2 major_len)); with both lengths
// below 2^32 every product here stays below 2^64. A major step past
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
// unclipped stepping, so the pixels drawn never depend on the clip; it
// starts at its first step that reaches the clip's near edges, so an
// endpoint far off the surface costs nothing extra.
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
            w.done = true; // walked past the clip's far edge; nothing further can draw
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
 * clipped; the pixels drawn inside it are the same wherever the endpoints
 * lie.
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
            break; // rendering is left-to-right; nothing further can draw
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
 * Renders @p text left-to-right starting with its top-left corner at
 * (@p x, @p y). Only glyph "ink" pixels are written — background pixels
 * within the cell are left untouched, so text composes over existing
 * content (on a monochrome surface, pass `ink = false` to erase ink pixels
 * instead, e.g. for inverse text on a filled banner). Characters outside
 * the font's range advance the cursor without drawing. Pixels falling
 * outside the surface are clipped, so text may start left of or above it.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the first glyph cell, in pixels; may be negative or off the
 *                surface.
 * @param y       Top edge of the glyph cells, in pixels; may be negative or off the surface.
 * @param text    The text to draw.
 * @param ink     The pixel value to write for glyph ink.
 * @param scale   Integer magnification factor (>= 1; 0 is treated as 1);
 *                each font pixel becomes a scale x scale block.
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
 * Equivalent to @ref draw_text with `ink = true`; see there for the full
 * rendering contract.
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
// top-left corner at (x, y). The position is wide so a text box can place
// lines and hyphens past the int range exactly: ink overhanging their
// position may still reach the surface.
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
        return; // no glyph's ink reaches the clip rows
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
        const auto [cols, rows] = clip_glyph(clip, pen, baseline, *g);
        pen += g->advance;
        const size_t bpr = g->bytes_per_row();
        const uint8_t* bitmap = font.glyph_bitmap(*g);
        for (size_t row = rows.first; row < rows.last; ++row) {
            const uint8_t* row_bits = bitmap + row * bpr;
            const size_t py = rows.base + row;
            for (size_t col = cols.first; col < cols.last; ++col) {
                if (row_bits[col / 8] & (0x80u >> (col % 8))) {
                    surface.set_pixel(cols.base + col, py, ink);
                }
            }
        }
    }
}

} // namespace detail
/** @endcond */

/**
 * @brief Draws text in a proportional font with the given ink.
 *
 * Decodes @p text as UTF-8 and renders it as one line, left-to-right, with
 * the line's top-left corner at (@p x, @p y): the baseline is
 * `font.ascent` rows below @p y, and each glyph moves the pen right by its
 * own advance, so @ref font::text_width gives the width to center or align
 * by. Only glyph "ink" pixels are written — the background is left
 * untouched, so text composes over existing content (on a monochrome
 * surface, pass `ink = false` to erase ink pixels instead). Code points
 * the font lacks are drawn as its replacement glyph, or skipped if it has
 * none; U+00AD SOFT HYPHEN is never drawn and takes no space; control
 * characters (including newlines) are not interpreted — see
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
 * Equivalent to @ref draw_text with `ink = true`; see there for the full
 * rendering contract.
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
// Offsets of text box lines from the box's corner are capped here: a line
// this far past an int position lies, with any ink its glyphs overhang,
// beyond every drawable rectangle.
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
 * @ref font::break_lines — wrapping at spaces and after hyphens,
 * hyphenating at soft hyphens (U+00AD), honoring `'\n'`, and splitting
 * words too long for a line — then draws each line as @ref draw_text does,
 * one `font.line_height` below the previous one, starting with the first
 * line's top edge at @p y. Each line is aligned within the columns
 * [@p x, @p x + @p width) by @p align; a line wider than the box (a single
 * character that does not fit) starts at @p x. A line broken at a soft
 * hyphen ends with a drawn `'-'`.
 *
 * The box limits line width only: all lines are drawn, however many there
 * are, and the return value gives the height they take, for placing what
 * follows. Rendering is ink-only and clips at the surface edges, as for
 * @ref draw_text.
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
 * Equivalent to @ref draw_text_box with `ink = true`; see there for the
 * full layout and rendering contract.
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
 * @ref font::break_lines — wrapping at spaces and after hyphens, honoring
 * `'\n'`, and splitting words too long for a line — then draws each line
 * as @ref draw_text does, `font.height * scale` rows below the previous
 * one, starting with the first line's top edge at @p y. Each line is
 * aligned within the columns [@p x, @p x + @p width) by @p align; a line
 * wider than the box (a single character that does not fit) starts at
 * @p x.
 *
 * The box limits line width only: all lines are drawn, however many there
 * are, and the return value gives the height they take, for placing what
 * follows. Rendering is ink-only and clips at the surface edges, as for
 * @ref draw_text.
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
 * @param scale   Integer magnification factor (>= 1; 0 is treated as 1);
 *                each font pixel becomes a scale x scale block.
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
 * Equivalent to @ref draw_text_box with `ink = true`; see there for the
 * full layout and rendering contract.
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
 * @brief A drawing view bundling a pixel surface with the drawing primitives.
 *
 * A canvas holds a reference to a surface and exposes the drawing primitives
 * as members, so a sequence of drawing calls reads as operations on one
 * object rather than free functions each taking the surface:
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
 * `fill`/`clear` when it provides them (framebuffers fill their backing
 * store directly), falling back to per-pixel writes otherwise. A canvas
 * only draws: to get the image on screen, write the surface it draws on to
 * the display (e.g. `panel.write(band, 0, 40)`).
 *
 * A canvas may also place its surface within a larger drawing-coordinate
 * space. Constructing with an origin and the space's size —
 * `canvas(band, 0, y, DISPLAY_W, DISPLAY_H)` — declares that the surface's
 * top-left corner sits at (0, y) in a DISPLAY_W x DISPLAY_H space, so a
 * frame taller than the surface can be authored once in full-screen
 * coordinates and rendered band by band, with everything outside the band
 * clipped (on all four sides — glyphs and lines straddling a band edge
 * render exactly their visible part) and @ref width / @ref height reporting
 * the full screen in every band:
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
 * @ref render_banded packages this loop — prefer it over writing the loop
 * by hand.
 *
 * The inverse mapping is @ref window, which returns a canvas for a
 * sub-region of this one with its own local coordinates and clipping —
 * e.g. handing a widget a canvas where (0, 0) is the widget's corner.
 *
 * A canvas is a cheap, copyable view: it does not own the surface, and the
 * caller must ensure the surface outlives it. A canvas itself satisfies
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
     * The canvas's coordinates coincide with the surface's: (0, 0) is the
     * surface's top-left corner, and @ref width / @ref height match the
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
     * @brief Creates a canvas for a @p width x @p height drawing space whose surface sits at
     *        (@p x, @p y) in it.
     *
     * Declares that the surface holds the region of the drawing space whose
     * top-left corner is (@p x, @p y) — the band-rendering mapping, with the
     * space's size given so that @ref width and @ref height report the whole
     * space (the full frame) wherever the surface sits. Drawing uses the
     * space's coordinates; pixels landing outside the surface, or outside
     * the space, are clipped on all four sides. A @p width or @p height
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
     * Coordinates at or beyond this bound clip. For a canvas covering its
     * whole surface this is the surface width; for a translated canvas it
     * is the drawing-space width it was constructed with; for a
     * @ref window it is the window width. It is at most `INT_MAX`.
     */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /**
     * @brief Returns the height of the canvas's coordinate space, in pixels.
     *
     * Coordinates at or beyond this bound clip. For a canvas covering its
     * whole surface this is the surface height; for a translated canvas it
     * is the drawing-space height it was constructed with; for a
     * @ref window it is the window height. It is at most `INT_MAX`.
     */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Returns a canvas for a sub-region of this one.
     *
     * The returned canvas has its own local coordinates — (0, 0) is the
     * sub-region's top-left corner, which sits at (@p x, @p y) on this
     * canvas — and clips to the sub-region's bounds, so drawing cannot
     * escape it (including @ref fill and @ref clear, which affect only the
     * sub-region). The sub-region may extend past any edge of this canvas
     * (@p x and @p y may be negative, e.g. for a widget sliding in from the
     * left); the part outside this canvas clips. Its far edges are clamped
     * to this canvas's bounds, so @ref width and @ref height report at most
     * the room left on this canvas, and to `INT_MAX` in the sub-region's own
     * coordinates, so the part of this canvas at local column or row
     * `INT_MAX` or beyond clips too; an empty result is allowed and draws
     * nothing.
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
     * Coordinates outside the canvas bounds, or landing outside the
     * underlying surface, are ignored.
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
     * Fills the canvas's drawable region — the part of its coordinate space
     * backed by the surface. Uses the surface's own `fill` when it provides
     * one and the region covers the whole surface (the identity and
     * whole-band cases); otherwise fills the region rectangle.
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
     * Clears the canvas's drawable region — the part of its coordinate
     * space backed by the surface. Uses the surface's own `clear` when it
     * provides one and the region covers the whole surface (the identity
     * and whole-band cases); otherwise fills the region with a
     * value-initialized pixel (false on monochrome surfaces, black on
     * RGB565 ones).
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
        // translated rectangle on the surface directly; the drawable region
        // lies inside the surface's own, so every write lands on it.
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
     * @p width columns and @p height rows; only its one-pixel border is
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
     * @brief Draws text with the given ink.
     *
     * Renders @p text left-to-right starting with its top-left corner at
     * (@p x, @p y). Only glyph "ink" pixels are written — background pixels
     * within the cell are left untouched, so text composes over existing
     * content (on a monochrome surface, pass `ink = false` to erase ink
     * pixels instead, e.g. for inverse text on a filled banner). Characters
     * outside the font's range advance the cursor without drawing. Pixels
     * falling outside the canvas are clipped.
     *
     * @param font  Font to render with.
     * @param x     Left edge of the first glyph cell, in pixels; may be negative or off the canvas.
     * @param y     Top edge of the glyph cells, in pixels; may be negative or off the canvas.
     * @param text  The text to draw.
     * @param ink   The pixel value to write for glyph ink.
     * @param scale Integer magnification factor (>= 1; 0 is treated as 1);
     *              each font pixel becomes a scale x scale block.
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
     * Equivalent to @ref draw_text with `ink = true`; see there for the full
     * rendering contract.
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
     * Decodes @p text as UTF-8 and renders it as one line, left-to-right,
     * with the line's top-left corner at (@p x, @p y): the baseline is
     * `font.ascent` rows below @p y, and each glyph moves the pen right by
     * its own advance, so @ref font::text_width gives the width to center
     * or align by. Only glyph "ink" pixels are written — the background is
     * left untouched (on a monochrome surface, pass `ink = false` to erase
     * ink pixels instead). Code points the font lacks are drawn as its
     * replacement glyph, or skipped if it has none; U+00AD SOFT HYPHEN is
     * never drawn and takes no space; control characters (including
     * newlines) are not interpreted — see @ref draw_text_box for wrapped,
     * multi-line text. Pixels falling outside the canvas are clipped.
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
     * Equivalent to @ref draw_text with `ink = true`; see there for the full
     * rendering contract.
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
     * @ref font::break_lines — wrapping at spaces and after hyphens,
     * hyphenating at soft hyphens (U+00AD), honoring `'\n'`, and splitting
     * words too long for a line — then draws each line as @ref draw_text
     * does, one `font.line_height` below the previous one, starting with the
     * first line's top edge at @p y. Each line is aligned within the columns
     * [@p x, @p x + @p width) by @p align; a line wider than the box (a
     * single character that does not fit) starts at @p x. A line broken at a
     * soft hyphen ends with a drawn `'-'`. All lines are drawn, however many
     * there are; pixels falling outside the canvas are clipped.
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
     * Equivalent to @ref draw_text_box with `ink = true`; see there for the
     * full layout and rendering contract.
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
     * @ref font::break_lines — wrapping at spaces and after hyphens,
     * honoring `'\n'`, and splitting words too long for a line — then draws
     * each line as @ref draw_text does, `font.height * scale` rows below the
     * previous one, starting with the first line's top edge at @p y. Each
     * line is aligned within the columns [@p x, @p x + @p width) by
     * @p align; a line wider than the box (a single character that does not
     * fit) starts at @p x. All lines are drawn, however many there are;
     * pixels falling outside the canvas are clipped.
     *
     * @param font  Font to render with.
     * @param x     Left edge of the box, in pixels; may be negative or off the canvas.
     * @param y     Top edge of the first line, in pixels; may be negative or off the canvas.
     * @param width Width of the box, in pixels.
     * @param text  The text to draw.
     * @param ink   The pixel value to write for glyph ink.
     * @param align Horizontal alignment of each line within the box.
     * @param scale Integer magnification factor (>= 1; 0 is treated as 1);
     *              each font pixel becomes a scale x scale block.
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
     * Equivalent to @ref draw_text_box with `ink = true`; see there for the
     * full layout and rendering contract.
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
    // and whole-band cases), enabling the surface's native fill/clear.
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
 * Quarter turns only: the two directions a portrait panel can be mounted
 * to serve a landscape layout (and vice versa). Each value names the turn
 * applied to the surface at mounting, so the adapter's remapping undoes it.
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
 * by a quarter turn — the common ePaper case of a portrait-native panel
 * mounted landscape. The view swaps the reported dimensions and remaps
 * @ref set_pixel so drawing happens in viewer coordinates while the
 * underlying surface keeps its native layout; the framebuffer behind the
 * view is written to the display in its native layout
 * (`display.write(fb)`).
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
 * Like a canvas, a rotated view is a cheap, copyable view: it does not own
 * the surface, and the caller must ensure the surface outlives it. It
 * satisfies @ref pixel_surface itself, with the same clipping contract —
 * writes outside its (swapped) bounds are ignored. @ref fill and @ref clear
 * forward to the surface's own when it provides them.
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

    /** @brief Returns the view's width — the surface's height. */
    [[nodiscard]] size_t width() const noexcept { return _surface->height(); }

    /** @brief Returns the view's height — the surface's width. */
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
 * Renders a @p frame_height-row frame through @p band, a framebuffer
 * covering `band.width()` x `band.height()` pixels, in
 * `frame_height / band.height()` passes. Each pass clears the band, invokes
 * @p draw with a canvas whose coordinates span the full frame (the band
 * placed at its slice via `canvas(band, 0, y, band.width(), frame_height)`,
 * so its width and height are the frame's in every pass), and writes the
 * band to @p dest at (0, y) with `dest.try_write(band, 0, y)`. Content
 * outside the pass's band is clipped before drawing rather than discarded
 * pixel by pixel; content straddling band edges renders exactly its visible
 * part, so the assembled frame is identical to one drawn through a
 * full-frame buffer.
 *
 * The callback is invoked once per band and must draw the complete frame
 * each time, as a pure function of the scene state — compute any state
 * changes (advancing samples, reading sensors) before rendering, not inside
 * the callback.
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
 * @param dest         The destination the band is written to after each pass.
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
 * Renders a @p frame_height-row frame through @p band, a framebuffer
 * covering `band.width()` x `band.height()` pixels, in
 * `frame_height / band.height()` passes. Each pass clears the band, invokes
 * @p draw with a canvas whose coordinates span the full frame (the band
 * placed at its slice via `canvas(band, 0, y, band.width(), frame_height)`,
 * so its width and height are the frame's in every pass), and writes the
 * band to @p dest at (0, y) with `dest.try_write(band, 0, y)`. Content
 * outside the pass's band is clipped before drawing rather than discarded
 * pixel by pixel; content straddling band edges renders exactly its visible
 * part, so the assembled frame is identical to one drawn through a
 * full-frame buffer.
 *
 * The callback is invoked once per band and must draw the complete frame
 * each time, as a pure function of the scene state — compute any state
 * changes (advancing samples, reading sensors) before rendering, not inside
 * the callback.
 *
 * @tparam Surface The band's surface type (satisfies @ref pixel_surface).
 * @tparam Dest    The destination type (e.g. a panel), providing
 *                 `try_write(const Surface&, size_t x, size_t y)`.
 * @tparam DrawFn  The draw callback type, invocable with `canvas<Surface>&`.
 * @param band         The framebuffer to render through; its width is the frame width.
 * @param dest         The destination the band is written to after each pass.
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
} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT
