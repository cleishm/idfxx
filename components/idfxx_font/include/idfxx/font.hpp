// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/font>
 * @file font.hpp
 * @brief Bitmap font rendering for monochrome framebuffers.
 *
 * @defgroup idfxx_font Font Component
 * @brief Fixed-cell bitmap fonts and text drawing for @ref idfxx_lcd
 *        monochrome framebuffers.
 *
 * Provides a compact fixed-cell font representation, ASCII text drawing with
 * integer scaling, and the Spleen fonts (5x8 and 8x16) ready to use. Each
 * bundled font lives in its own translation unit, so fonts an application
 * never references are dropped at link time.
 * @{
 */

#include <idfxx/lcd/mono_framebuffer.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @headerfile <idfxx/font>
 * @brief Bitmap font types, drawing functions, and bundled fonts.
 */
namespace idfxx::font {

/**
 * @headerfile <idfxx/font>
 * @brief A fixed-cell monochrome bitmap font.
 *
 * Glyphs cover a contiguous character range and share one cell size. The
 * bitmap stores glyphs consecutively, each as `height` rows of
 * `ceil(width / 8)` bytes, MSB = leftmost pixel.
 */
struct mono_font {
    uint8_t width;         ///< Glyph cell width in pixels.
    uint8_t height;        ///< Glyph cell height in pixels.
    char first_char;       ///< First character in the glyph range.
    char last_char;        ///< Last character in the glyph range (inclusive).
    const uint8_t* bitmap; ///< Glyph data (see struct description for layout).

    /** @brief Returns the number of bytes in one glyph row. */
    [[nodiscard]] constexpr size_t bytes_per_row() const noexcept { return (width + 7u) / 8u; }

    /** @brief Returns the number of bytes in one glyph. */
    [[nodiscard]] constexpr size_t bytes_per_glyph() const noexcept { return bytes_per_row() * height; }

    /** @brief Returns true if the font has a glyph for @p c. */
    [[nodiscard]] constexpr bool contains(char c) const noexcept { return c >= first_char && c <= last_char; }

    /**
     * @brief Returns a pointer to the glyph bitmap for @p c.
     * @param c A character satisfying @ref contains.
     * @return The first byte of the glyph's rows.
     */
    [[nodiscard]] constexpr const uint8_t* glyph(char c) const noexcept {
        return bitmap + static_cast<size_t>(c - first_char) * bytes_per_glyph();
    }
};

/// Spleen 5x8 — compact status text (BSD-2-Clause, see fonts/LICENSE).
extern const mono_font spleen_5x8;

/// Spleen 8x16 — headline text; scale 2 gives 16x32 digits (BSD-2-Clause).
extern const mono_font spleen_8x16;

/**
 * @brief Returns the width in pixels of @p text rendered in @p font.
 *
 * Every character advances one cell, including characters outside the font's
 * glyph range (drawn blank by @ref draw_text).
 *
 * @param font  The font.
 * @param text  The text to measure.
 * @param scale Integer magnification factor (>= 1).
 * @return The rendered width in pixels.
 */
[[nodiscard]] constexpr size_t text_width(const mono_font& font, std::string_view text, unsigned scale = 1) noexcept {
    return text.size() * font.width * scale;
}

/**
 * @brief Draws text into a monochrome framebuffer.
 *
 * Renders @p text left-to-right starting with its top-left corner at
 * (@p x, @p y). Only glyph "ink" pixels are written — background pixels
 * within the cell are left untouched, so text composes over existing
 * content. Pass `on = false` to erase ink pixels instead (e.g. text on a
 * filled banner). Characters outside the font's range advance the cursor
 * without drawing. Pixels falling outside the framebuffer are clipped.
 *
 * @param fb    Target framebuffer.
 * @param font  Font to render with.
 * @param x     Left edge of the first glyph cell, in pixels.
 * @param y     Top edge of the glyph cells, in pixels.
 * @param text  The text to draw.
 * @param on    Pixel state to write for glyph ink (true = set).
 * @param scale Integer magnification factor (>= 1); each font pixel becomes
 *              a scale x scale block.
 */
void draw_text(
    lcd::mono_framebuffer& fb,
    const mono_font& font,
    size_t x,
    size_t y,
    std::string_view text,
    bool on = true,
    unsigned scale = 1
) noexcept;

/** @} */ // end of idfxx_font

} // namespace idfxx::font
