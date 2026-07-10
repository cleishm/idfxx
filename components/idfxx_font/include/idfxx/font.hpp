// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/font>
 * @file font.hpp
 * @brief Bitmap font representation and text metrics.
 *
 * @defgroup idfxx_font Font Component
 * @brief Fixed-cell bitmap font model and text metrics.
 *
 * Provides a compact fixed-cell font representation and text measurement.
 * Rendering lives in the @ref idfxx_gfx component; bundled fonts are
 * published as separate data packages (e.g. `idfxx_font_spleen`), each font
 * in its own translation unit so fonts an application never references are
 * dropped at link time. A BDF-to-C converter script for adding more fonts is
 * included under `scripts/`.
 * @{
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @headerfile <idfxx/font>
 * @brief Bitmap font types, text metrics, and bundled fonts.
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

    /**
     * @brief Returns true if the glyph for @p c has ink at (@p col, @p row).
     *
     * Per-pixel accessor decoding the glyph bitmap layout, so consumers
     * need not address the packed bytes themselves.
     *
     * @param c   A character satisfying @ref contains.
     * @param col Column within the glyph cell, in `[0, width)`.
     * @param row Row within the glyph cell, in `[0, height)`.
     * @return true if the pixel is inked.
     */
    [[nodiscard]] constexpr bool ink_at(char c, size_t col, size_t row) const noexcept {
        return glyph(c)[row * bytes_per_row() + col / 8] & (0x80u >> (col % 8));
    }

    /**
     * @brief Returns the horizontal distance the cursor moves after @p c.
     *
     * In a fixed-cell font every character advances one cell width,
     * including characters outside the glyph range (rendered blank).
     *
     * @param c The character.
     * @return The advance in pixels.
     */
    [[nodiscard]] constexpr size_t advance(char /*c*/) const noexcept { return width; }
};

/**
 * @brief Returns the width in pixels of @p text rendered in @p font.
 *
 * Sums the advance of every character in @p text; characters outside the
 * font's glyph range advance like any other (rendered blank).
 *
 * @param font  The font.
 * @param text  The text to measure.
 * @param scale Integer magnification factor (>= 1; 0 is treated as 1).
 * @return The rendered width in pixels.
 */
[[nodiscard]] constexpr size_t text_width(const mono_font& font, std::string_view text, unsigned scale = 1) noexcept {
    if (scale == 0) {
        scale = 1;
    }
    size_t width = 0;
    for (char c : text) {
        width += font.advance(c);
    }
    return width * scale;
}

/** @} */ // end of idfxx_font

} // namespace idfxx::font
