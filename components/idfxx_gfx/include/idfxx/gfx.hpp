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
 * The primitives are available two ways: as members of @ref
 * idfxx::gfx::canvas, a lightweight view bundling a surface with the drawing
 * operations, and as free functions taking the surface as their first
 * argument.
 *
 * @code
 * idfxx::lcd::mono_framebuffer fb(display.width(), display.height());
 * idfxx::gfx::canvas canvas(fb);
 *
 * canvas.fill_rect(0, 0, canvas.width(), 10, true);
 * canvas.draw_text(idfxx::font::spleen_5x8, 2, 1, "status", false);
 * canvas.draw_text(idfxx::font::spleen_8x16, 4, 16, "23.7", true, 2);
 * canvas.flush(display);
 * @endcode
 * @{
 */

#include <idfxx/error.hpp>
#include <idfxx/font.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
concept pixel_surface = requires(S& s, const S& cs, size_t x, size_t y, typename S::pixel_type pixel) {
    typename S::pixel_type;
    { s.set_pixel(x, y, pixel) } noexcept;
    { cs.width() } -> std::convertible_to<size_t>;
    { cs.height() } -> std::convertible_to<size_t>;
};

/**
 * @brief Fills a rectangle with the given ink.
 *
 * The rectangle's top-left corner is at (@p x, @p y) and it spans @p width
 * columns and @p height rows. Any part falling outside the surface is
 * clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the rectangle, in pixels.
 * @param y       Top edge of the rectangle, in pixels.
 * @param width   Width of the rectangle, in pixels.
 * @param height  Height of the rectangle, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void fill_rect(
    Surface& surface,
    size_t x,
    size_t y,
    size_t width,
    size_t height,
    typename Surface::pixel_type ink
) noexcept {
    const size_t surface_width = surface.width();
    const size_t surface_height = surface.height();
    if (x >= surface_width || y >= surface_height) {
        return;
    }
    const size_t x_end = x + std::min(width, surface_width - x);
    const size_t y_end = y + std::min(height, surface_height - y);
    for (size_t py = y; py < y_end; ++py) {
        for (size_t px = x; px < x_end; ++px) {
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
 * @param x       Column of the line's left end, in pixels.
 * @param y       Row of the line, in pixels.
 * @param length  Length of the line, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_hline(Surface& surface, size_t x, size_t y, size_t length, typename Surface::pixel_type ink) noexcept {
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
 * @param x       Column of the line, in pixels.
 * @param y       Row of the line's top end, in pixels.
 * @param length  Length of the line, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_vline(Surface& surface, size_t x, size_t y, size_t length, typename Surface::pixel_type ink) noexcept {
    fill_rect(surface, x, y, 1, length, ink);
}

/**
 * @brief Outlines a rectangle with the given ink.
 *
 * The rectangle's top-left corner is at (@p x, @p y) and it spans @p width
 * columns and @p height rows; only its one-pixel border is drawn. Any part
 * falling outside the surface is clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x       Left edge of the rectangle, in pixels.
 * @param y       Top edge of the rectangle, in pixels.
 * @param width   Width of the rectangle, in pixels.
 * @param height  Height of the rectangle, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_rect(
    Surface& surface,
    size_t x,
    size_t y,
    size_t width,
    size_t height,
    typename Surface::pixel_type ink
) noexcept {
    if (width == 0 || height == 0) {
        return;
    }
    if (width <= 2 || height <= 2) {
        fill_rect(surface, x, y, width, height, ink);
        return;
    }
    draw_hline(surface, x, y, width, ink);
    draw_hline(surface, x, y + height - 1, width, ink);
    draw_vline(surface, x, y + 1, height - 2, ink);
    draw_vline(surface, x + width - 1, y + 1, height - 2, ink);
}

/**
 * @brief Draws a straight line between two points with the given ink.
 *
 * Both endpoints are inclusive. Any part falling outside the surface is
 * clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param x0      Column of the first endpoint, in pixels.
 * @param y0      Row of the first endpoint, in pixels.
 * @param x1      Column of the second endpoint, in pixels.
 * @param y1      Row of the second endpoint, in pixels.
 * @param ink     The pixel value to write.
 */
template<pixel_surface Surface>
void draw_line(
    Surface& surface,
    size_t x0,
    size_t y0,
    size_t x1,
    size_t y1,
    typename Surface::pixel_type ink
) noexcept {
    if (std::min(x0, x1) >= surface.width() || std::min(y0, y1) >= surface.height()) {
        return; // the endpoints' bounding box lies entirely off the surface
    }
    // Bresenham's algorithm; the walk stays within the endpoints' bounding
    // box, so intermediate coordinates never go negative.
    ptrdiff_t px = static_cast<ptrdiff_t>(x0);
    ptrdiff_t py = static_cast<ptrdiff_t>(y0);
    const ptrdiff_t ex = static_cast<ptrdiff_t>(x1);
    const ptrdiff_t ey = static_cast<ptrdiff_t>(y1);
    const ptrdiff_t dx = std::abs(ex - px);
    const ptrdiff_t dy = -std::abs(ey - py);
    const ptrdiff_t sx = px < ex ? 1 : -1;
    const ptrdiff_t sy = py < ey ? 1 : -1;
    ptrdiff_t err = dx + dy;
    while (true) {
        surface.set_pixel(static_cast<size_t>(px), static_cast<size_t>(py), ink);
        if (px == ex && py == ey) {
            break;
        }
        const ptrdiff_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            px += sx;
        }
        if (e2 <= dx) {
            err += dx;
            py += sy;
        }
    }
}

/**
 * @brief Draws text with the given ink.
 *
 * Renders @p text left-to-right starting with its top-left corner at
 * (@p x, @p y). Only glyph "ink" pixels are written — background pixels
 * within the cell are left untouched, so text composes over existing
 * content (on a monochrome surface, pass `ink = false` to erase ink pixels
 * instead, e.g. for inverse text on a filled banner). Characters outside
 * the font's range advance the cursor without drawing. Pixels falling
 * outside the surface are clipped.
 *
 * @tparam Surface The surface type (satisfies @ref pixel_surface).
 * @param surface The surface to draw on.
 * @param font    Font to render with.
 * @param x       Left edge of the first glyph cell, in pixels.
 * @param y       Top edge of the glyph cells, in pixels.
 * @param text    The text to draw.
 * @param ink     The pixel value to write for glyph ink.
 * @param scale   Integer magnification factor (>= 1; 0 is treated as 1);
 *                each font pixel becomes a scale x scale block.
 */
template<pixel_surface Surface>
void draw_text(
    Surface& surface,
    const font::mono_font& font,
    size_t x,
    size_t y,
    std::string_view text,
    typename Surface::pixel_type ink,
    unsigned scale = 1
) noexcept {
    if (scale == 0) {
        scale = 1;
    }
    if (y >= surface.height()) {
        return; // every glyph row would clip
    }
    const size_t bpr = font.bytes_per_row();
    size_t cell_x = x;

    for (char c : text) {
        if (cell_x >= surface.width()) {
            break; // rendering is left-to-right; nothing further can draw
        }
        if (font.contains(c)) {
            const uint8_t* glyph = font.glyph(c);
            for (size_t row = 0; row < font.height; ++row) {
                const uint8_t* row_bits = glyph + row * bpr;
                uint8_t bits = 0;
                for (size_t col = 0; col < font.width; ++col, bits <<= 1) {
                    if (col % 8 == 0) {
                        bits = row_bits[col / 8];
                    }
                    if (!(bits & 0x80u)) {
                        continue;
                    }
                    const size_t px = cell_x + col * scale;
                    const size_t py = y + row * scale;
                    for (unsigned sy = 0; sy < scale; ++sy) {
                        for (unsigned sx = 0; sx < scale; ++sx) {
                            surface.set_pixel(px + sx, py + sy, ink);
                        }
                    }
                }
            }
        }
        cell_x += font.advance(c) * scale;
    }
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
 * @param x       Left edge of the first glyph cell, in pixels.
 * @param y       Top edge of the glyph cells, in pixels.
 * @param text    The text to draw.
 */
template<pixel_surface Surface>
    requires std::same_as<typename Surface::pixel_type, bool>
void draw_text(Surface& surface, const font::mono_font& font, size_t x, size_t y, std::string_view text) noexcept {
    draw_text(surface, font, x, y, text, true);
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
 * canvas.flush(panel, 0, 40);
 * @endcode
 *
 * Each drawing member is equivalent to the free function of the same name
 * applied to the underlying surface, with the same ink-only rendering and
 * clipping contract. @ref fill and @ref clear use the surface's own
 * `fill`/`clear` when it provides them (framebuffers fill their backing
 * store directly), falling back to per-pixel writes otherwise. When the
 * surface can push its content onward (the framebuffers' `flush` /
 * `try_flush` to a panel), @ref flush and @ref try_flush forward to it, so
 * the full draw-then-transfer cycle reads off the one object.
 *
 * A canvas may also place its surface within a larger drawing-coordinate
 * space. Constructing with an origin — `canvas(band, 0, y)` — declares that
 * the surface's top-left corner sits at (0, y) in the canvas's coordinates,
 * so a frame taller than the surface can be authored once in full-screen
 * coordinates and rendered band by band, with everything outside the band
 * clipped (on all four sides — glyphs and lines straddling a band edge
 * render exactly their visible part):
 *
 * @code
 * idfxx::lcd::rgb565_framebuffer band(DISPLAY_W, BAND_H);
 * for (size_t y = 0; y < DISPLAY_H; y += BAND_H) {
 *     idfxx::gfx::canvas canvas(band, 0, y);
 *     canvas.clear();
 *     draw_frame(canvas); // draws in full-screen coordinates
 *     canvas.flush(panel, 0, y);
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
     * surface's dimensions (captured at construction).
     *
     * @param surface The surface to draw on; must outlive the canvas.
     */
    explicit canvas(Surface& surface) noexcept
        : _surface(&surface)
        , _dx(0)
        , _dy(0)
        , _width(surface.width())
        , _height(surface.height()) {}

    /**
     * @brief Creates a canvas whose surface sits at (@p x, @p y) in the canvas's coordinates.
     *
     * Declares that the surface holds the region of the drawing space whose
     * top-left corner is (@p x, @p y) — the band-rendering mapping. Drawing
     * uses the full-space coordinates; pixels landing outside the surface
     * are clipped on all four sides. @ref width and @ref height report the
     * far edges of the surface in canvas coordinates (@p x + the surface
     * width, @p y + the surface height), so for a full-width band they
     * match the frame dimensions.
     *
     * @param surface The surface to draw on; must outlive the canvas.
     * @param x       Canvas-coordinate column of the surface's left edge.
     * @param y       Canvas-coordinate row of the surface's top edge.
     */
    canvas(Surface& surface, size_t x, size_t y) noexcept
        : _surface(&surface)
        , _dx(static_cast<ptrdiff_t>(x))
        , _dy(static_cast<ptrdiff_t>(y))
        , _width(x + surface.width())
        , _height(y + surface.height()) {}

    /** @brief Returns the underlying surface. */
    [[nodiscard]] Surface& surface() const noexcept { return *_surface; }

    /**
     * @brief Returns the width of the canvas's coordinate space, in pixels.
     *
     * Coordinates at or beyond this bound clip. For a canvas covering its
     * whole surface this is the surface width; for a translated canvas it
     * is the surface's far column edge in canvas coordinates; for a
     * @ref window it is the window width.
     */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /**
     * @brief Returns the height of the canvas's coordinate space, in pixels.
     *
     * Coordinates at or beyond this bound clip. For a canvas covering its
     * whole surface this is the surface height; for a translated canvas it
     * is the surface's far row edge in canvas coordinates; for a
     * @ref window it is the window height.
     */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Returns a canvas for a sub-region of this one.
     *
     * The returned canvas has its own local coordinates — (0, 0) is the
     * sub-region's top-left corner, which sits at (@p x, @p y) on this
     * canvas — and clips to the sub-region's bounds, so drawing cannot
     * escape it (including @ref fill and @ref clear, which affect only the
     * sub-region). The requested rectangle is clamped to this canvas's
     * bounds; an empty result is allowed and draws nothing.
     *
     * @code
     * auto gauge = canvas.window(50, 60, 100, 30);
     * gauge.clear();
     * gauge.draw_text(idfxx::font::spleen_5x8, 2, 2, "RPM", white);
     * @endcode
     *
     * @param x      Column of the sub-region's left edge, in this canvas's coordinates.
     * @param y      Row of the sub-region's top edge, in this canvas's coordinates.
     * @param width  Width of the sub-region, in pixels.
     * @param height Height of the sub-region, in pixels.
     * @return A canvas over the same surface, restricted to the sub-region.
     */
    [[nodiscard]] canvas window(size_t x, size_t y, size_t width, size_t height) const noexcept {
        canvas sub(*this);
        sub._dx = _dx - static_cast<ptrdiff_t>(x);
        sub._dy = _dy - static_cast<ptrdiff_t>(y);
        sub._width = x < _width ? std::min(width, _width - x) : 0;
        sub._height = y < _height ? std::min(height, _height - y) : 0;
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
        if (x >= _width || y >= _height) {
            return;
        }
        // Coordinates left of or above the surface wrap to huge values and
        // are ignored by the surface's own bounds check.
        _surface->set_pixel(
            static_cast<size_t>(static_cast<ptrdiff_t>(x) - _dx),
            static_cast<size_t>(static_cast<ptrdiff_t>(y) - _dy),
            ink
        );
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
     * @brief Pushes the surface's content onward, on surfaces that support it.
     *
     * Forwards to the underlying surface's `flush`, so a canvas over a
     * framebuffer completes the draw-then-transfer cycle without reaching
     * for the surface: `canvas.flush(panel, x, y)`.
     *
     * @tparam Args Argument types accepted by the surface's `flush`.
     * @param args Arguments forwarded to the surface's `flush`.
     * @return Whatever the surface's `flush` returns.
     * @note Only declared when the surface provides a matching `flush`
     *       overload (for the idfxx framebuffers, only when
     *       CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig).
     * @throws Whatever the surface's `flush` throws (std::system_error for
     *         the idfxx framebuffers).
     */
    template<typename... Args>
        requires requires(Surface& s) { s.flush(std::declval<Args>()...); }
    decltype(auto) flush(Args&&... args) const {
        return _surface->flush(std::forward<Args>(args)...);
    }

    /**
     * @brief Pushes the surface's content onward, on surfaces that support it.
     *
     * Forwards to the underlying surface's `try_flush`, so a canvas over a
     * framebuffer completes the draw-then-transfer cycle without reaching
     * for the surface: `canvas.try_flush(panel, x, y)`.
     *
     * @tparam Args Argument types accepted by the surface's `try_flush`.
     * @param args Arguments forwarded to the surface's `try_flush`.
     * @return Whatever the surface's `try_flush` returns (success or an
     *         error for the idfxx framebuffers).
     * @note Only declared when the surface provides a matching `try_flush`
     *       overload.
     */
    template<typename... Args>
        requires requires(Surface& s) { s.try_flush(std::declval<Args>()...); }
    [[nodiscard]] decltype(auto) try_flush(Args&&... args) const {
        return _surface->try_flush(std::forward<Args>(args)...);
    }

    /**
     * @brief Fills a rectangle with the given ink.
     *
     * The rectangle's top-left corner is at (@p x, @p y) and it spans
     * @p width columns and @p height rows. Any part falling outside the
     * canvas is clipped.
     *
     * @param x      Left edge of the rectangle, in pixels.
     * @param y      Top edge of the rectangle, in pixels.
     * @param width  Width of the rectangle, in pixels.
     * @param height Height of the rectangle, in pixels.
     * @param ink    The pixel value to write.
     */
    void fill_rect(size_t x, size_t y, size_t width, size_t height, pixel_type ink) noexcept {
        // Clamp the start to the drawable region in canvas coordinates so
        // the translated start is never left of or above the surface; the
        // surface-level fill_rect clips the far edges.
        const size_t sx = std::max(x, _dx > 0 ? static_cast<size_t>(_dx) : size_t{0});
        const size_t sy = std::max(y, _dy > 0 ? static_cast<size_t>(_dy) : size_t{0});
        if (sx >= _width || sy >= _height || sx - x >= width || sy - y >= height) {
            return;
        }
        gfx::fill_rect(
            *_surface,
            static_cast<size_t>(static_cast<ptrdiff_t>(sx) - _dx),
            static_cast<size_t>(static_cast<ptrdiff_t>(sy) - _dy),
            std::min(width - (sx - x), _width - sx),
            std::min(height - (sy - y), _height - sy),
            ink
        );
    }

    /**
     * @brief Draws a horizontal line with the given ink.
     *
     * The line starts at (@p x, @p y) and extends @p length pixels to the
     * right. Any part falling outside the canvas is clipped.
     *
     * @param x      Column of the line's left end, in pixels.
     * @param y      Row of the line, in pixels.
     * @param length Length of the line, in pixels.
     * @param ink    The pixel value to write.
     */
    void draw_hline(size_t x, size_t y, size_t length, pixel_type ink) noexcept { fill_rect(x, y, length, 1, ink); }

    /**
     * @brief Draws a vertical line with the given ink.
     *
     * The line starts at (@p x, @p y) and extends @p length pixels downward.
     * Any part falling outside the canvas is clipped.
     *
     * @param x      Column of the line, in pixels.
     * @param y      Row of the line's top end, in pixels.
     * @param length Length of the line, in pixels.
     * @param ink    The pixel value to write.
     */
    void draw_vline(size_t x, size_t y, size_t length, pixel_type ink) noexcept { fill_rect(x, y, 1, length, ink); }

    /**
     * @brief Outlines a rectangle with the given ink.
     *
     * The rectangle's top-left corner is at (@p x, @p y) and it spans
     * @p width columns and @p height rows; only its one-pixel border is
     * drawn. Any part falling outside the canvas is clipped.
     *
     * @param x      Left edge of the rectangle, in pixels.
     * @param y      Top edge of the rectangle, in pixels.
     * @param width  Width of the rectangle, in pixels.
     * @param height Height of the rectangle, in pixels.
     * @param ink    The pixel value to write.
     */
    void draw_rect(size_t x, size_t y, size_t width, size_t height, pixel_type ink) noexcept {
        gfx::draw_rect(*this, x, y, width, height, ink);
    }

    /**
     * @brief Draws a straight line between two points with the given ink.
     *
     * Both endpoints are inclusive. Any part falling outside the canvas is
     * clipped.
     *
     * @param x0  Column of the first endpoint, in pixels.
     * @param y0  Row of the first endpoint, in pixels.
     * @param x1  Column of the second endpoint, in pixels.
     * @param y1  Row of the second endpoint, in pixels.
     * @param ink The pixel value to write.
     */
    void draw_line(size_t x0, size_t y0, size_t x1, size_t y1, pixel_type ink) noexcept {
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
     * @param x     Left edge of the first glyph cell, in pixels.
     * @param y     Top edge of the glyph cells, in pixels.
     * @param text  The text to draw.
     * @param ink   The pixel value to write for glyph ink.
     * @param scale Integer magnification factor (>= 1; 0 is treated as 1);
     *              each font pixel becomes a scale x scale block.
     */
    void draw_text(
        const font::mono_font& font,
        size_t x,
        size_t y,
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
     * @param x    Left edge of the first glyph cell, in pixels.
     * @param y    Top edge of the glyph cells, in pixels.
     * @param text The text to draw.
     */
    void draw_text(const font::mono_font& font, size_t x, size_t y, std::string_view text) noexcept
        requires std::same_as<pixel_type, bool>
    {
        gfx::draw_text(*this, font, x, y, text);
    }

private:
    // True when the drawable region spans the entire surface (the identity
    // and whole-band cases), enabling the surface's native fill/clear.
    [[nodiscard]] bool _covers_surface() const noexcept {
        return _dx >= 0 && _dy >= 0 && static_cast<size_t>(_dx) + _surface->width() <= _width &&
            static_cast<size_t>(_dy) + _surface->height() <= _height;
    }

    Surface* _surface;
    ptrdiff_t _dx; // canvas-coordinate position of the surface's (0, 0)
    ptrdiff_t _dy;
    size_t _width; // canvas-coordinate clip bounds
    size_t _height;
};

/**
 * @cond INTERNAL
 * @brief Constraint for the banded-rendering functions: the draw callback
 * accepts a canvas over the band, and the band can flush to the destination
 * at an offset.
 */
template<typename Surface, typename Dest, typename DrawFn>
concept banded_renderable = pixel_surface<Surface> && requires(canvas<Surface>& c, Dest& dest, DrawFn& draw) {
    draw(c);
    { c.try_flush(dest, size_t{}, size_t{}) } -> std::same_as<result<void>>;
};
/** @endcond */

/** @cond INTERNAL */
template<pixel_surface Surface, typename Dest, typename DrawFn>
    requires banded_renderable<Surface, Dest, DrawFn>
[[nodiscard]] result<void> try_render_banded(Surface& band, Dest& dest, size_t frame_height, DrawFn&& draw);
/** @endcond */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Renders a frame taller than its framebuffer, band by band.
 *
 * Renders a @p frame_height-row frame through @p band, a framebuffer
 * covering `band.width()` x `band.height()` pixels, in
 * `frame_height / band.height()` passes. Each pass clears the band, invokes
 * @p draw with a canvas whose coordinates span the full frame (the band
 * placed at its slice via `canvas(band, 0, y)`), and flushes the band to
 * @p dest at (0, y). Content outside the pass's band clips; content
 * straddling band edges renders exactly its visible part, so the assembled
 * frame is identical to one drawn through a full-frame buffer.
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
 * @tparam Dest    The flush destination type (e.g. a panel).
 * @tparam DrawFn  The draw callback type, invocable with `canvas<Surface>&`.
 * @param band         The framebuffer to render through; its width is the frame width.
 * @param dest         The destination the band flushes to after each pass.
 * @param frame_height Height of the frame, in pixels; must be a non-zero
 *                     multiple of `band.height()`.
 * @param draw         Callback drawing the complete frame on the given canvas.
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
 * @throws std::system_error on error (e.g. an invalid @p frame_height, or a
 *         failed flush).
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
 * placed at its slice via `canvas(band, 0, y)`), and flushes the band to
 * @p dest at (0, y). Content outside the pass's band clips; content
 * straddling band edges renders exactly its visible part, so the assembled
 * frame is identical to one drawn through a full-frame buffer.
 *
 * The callback is invoked once per band and must draw the complete frame
 * each time, as a pure function of the scene state — compute any state
 * changes (advancing samples, reading sensors) before rendering, not inside
 * the callback.
 *
 * @tparam Surface The band's surface type (satisfies @ref pixel_surface).
 * @tparam Dest    The flush destination type (e.g. a panel).
 * @tparam DrawFn  The draw callback type, invocable with `canvas<Surface>&`.
 * @param band         The framebuffer to render through; its width is the frame width.
 * @param dest         The destination the band flushes to after each pass.
 * @param frame_height Height of the frame, in pixels; must be a non-zero
 *                     multiple of `band.height()`.
 * @param draw         Callback drawing the complete frame on the given canvas.
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_arg if @p frame_height is zero or not a
 *         multiple of `band.height()`, or the band has zero height.
 */
template<pixel_surface Surface, typename Dest, typename DrawFn>
    requires banded_renderable<Surface, Dest, DrawFn>
[[nodiscard]] result<void> try_render_banded(Surface& band, Dest& dest, size_t frame_height, DrawFn&& draw) {
    const size_t band_height = band.height();
    if (band_height == 0 || frame_height == 0 || frame_height % band_height != 0) {
        return error(errc::invalid_arg);
    }
    for (size_t y = 0; y < frame_height; y += band_height) {
        canvas c(band, 0, y);
        c.clear();
        draw(c);
        if (auto flushed = c.try_flush(dest, size_t{0}, y); !flushed) {
            return flushed;
        }
    }
    return {};
}

/** @} */ // end of idfxx_gfx

} // namespace idfxx::gfx
