// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/font>
 * @file font.hpp
 * @brief Bitmap font representation and text metrics.
 *
 * @defgroup idfxx_font Font Component
 * @brief Bitmap font models and text metrics.
 *
 * Provides two compact font representations — fixed-cell (@ref idfxx::font::mono_font)
 * and proportional (@ref idfxx::font::proportional_font) — plus text measurement.
 * Rendering lives in the @ref idfxx_gfx component; bundled fonts are
 * published as separate data packages (e.g. `idfxx_font_spleen`), each font
 * in its own translation unit so fonts an application never references are
 * dropped at link time. Generators for adding more fonts are included under
 * `scripts/`: `bdf_to_c.py` (monospace BDF to @ref idfxx::font::mono_font) and
 * `ttf_to_c.py` (TrueType/OpenType to @ref idfxx::font::proportional_font).
 * @{
 */

#include <idfxx/utf8>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
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
    [[nodiscard]] constexpr size_t bytes_per_glyph() const noexcept {
        return bytes_per_row() * height;
    }

    /** @brief Returns true if the font has a glyph for @p c. */
    [[nodiscard]] constexpr bool contains(char c) const noexcept {
        return c >= first_char && c <= last_char;
    }

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
    [[nodiscard]] constexpr size_t advance([[maybe_unused]] char c) const noexcept { return width; }
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
[[nodiscard]] constexpr size_t
text_width(const mono_font& font, std::string_view text, unsigned scale = 1) noexcept {
    if (scale == 0) {
        scale = 1;
    }
    size_t width = 0;
    for (char c : text) {
        width += font.advance(c);
    }
    return width * scale;
}

/**
 * @headerfile <idfxx/font>
 * @brief Placement and bitmap location of one glyph in a @ref proportional_font.
 *
 * The glyph's bitmap covers only its inked bounding box, positioned
 * relative to the pen: with the pen at column `pen_x` on the baseline row
 * `baseline_y`, the bitmap's top-left pixel lands at
 * (`pen_x + x_offset`, `baseline_y + y_offset`). After drawing, the pen
 * moves right by `advance`.
 *
 * The bitmap stores `height` rows of `ceil(width / 8)` bytes, MSB = leftmost
 * pixel, starting `bitmap_offset` bytes into the font's bitmap. A glyph with
 * no ink (e.g. a space) has zero width and height.
 */
struct glyph_metrics {
    /// Byte offset of the glyph's first row within the font's bitmap.
    uint32_t bitmap_offset;
    /// Bitmap width in pixels.
    uint16_t width;
    /// Bitmap height in pixels.
    uint16_t height;
    /// Distance the pen moves right after this glyph, in pixels.
    uint16_t advance;
    /// Columns from the pen position to the bitmap's left edge (negative = left of the pen).
    int16_t x_offset;
    /// Rows from the baseline to the bitmap's top edge (negative = above the baseline).
    int16_t y_offset;

    /** @brief Returns the number of bytes in one bitmap row. */
    [[nodiscard]] constexpr size_t bytes_per_row() const noexcept { return (width + 7u) / 8u; }
};

/**
 * @headerfile <idfxx/font>
 * @brief A run of consecutive code points with consecutive glyphs.
 *
 * Code points `first` through `last` (inclusive) map to the glyphs starting
 * at index `first_glyph` of the font's glyph table, in order.
 */
struct glyph_range {
    char32_t first;       ///< First code point in the run.
    char32_t last;        ///< Last code point in the run (inclusive).
    uint32_t first_glyph; ///< Glyph table index of the glyph for @ref first.
};

/**
 * @headerfile <idfxx/font>
 * @brief A proportional monochrome bitmap font.
 *
 * Each glyph has its own advance and a bitmap trimmed to its inked bounding
 * box, so text sets naturally ("il" narrower than "mw") and large sizes
 * stay compact. Glyphs are rendered at one fixed size: a font is produced
 * at the pixel size it will be drawn at, typically by rasterizing an
 * outline font (TrueType/OpenType) with the `ttf_to_c.py` generator under
 * `scripts/`. Coverage is any set of Unicode code points, described by
 * @ref ranges.
 *
 * Text is laid out on lines: a line's top edge sits @ref ascent rows above
 * its baseline and its bottom edge @ref descent rows below; consecutive
 * lines are @ref line_height rows apart. Accents on capitals and other tall
 * marks may extend above the line's top edge.
 *
 * Code points the font has no glyph for are drawn and measured as the
 * @ref replacement glyph, when the font has one; otherwise they are
 * skipped entirely.
 */
struct proportional_font {
    /// Rows from the top of a line to its baseline.
    uint16_t ascent;
    /// Rows from the baseline to the bottom of a line.
    uint16_t descent;
    /// Rows from one line's baseline to the next (ascent + descent + line gap).
    uint16_t line_height;
    /// Covered code points, sorted ascending and non-overlapping.
    std::span<const glyph_range> ranges;
    /// Glyph table indexed by the ranges.
    std::span<const glyph_metrics> glyphs;
    /// Glyph used for code points without a glyph, or null to skip them.
    const glyph_metrics* replacement;
    /// Packed glyph bitmaps (see @ref glyph_metrics for the layout).
    const uint8_t* bitmap;

    /**
     * @brief Returns the font's own glyph for @p cp.
     * @param cp The code point.
     * @return The glyph, or null if the font does not cover @p cp.
     */
    [[nodiscard]] constexpr const glyph_metrics* find(char32_t cp) const noexcept {
        const auto it = std::ranges::lower_bound(ranges, cp, {}, &glyph_range::last);
        if (it == ranges.end() || cp < it->first) {
            return nullptr;
        }
        return &glyphs[it->first_glyph + (cp - it->first)];
    }

    /** @brief Returns true if the font has its own glyph for @p cp. */
    [[nodiscard]] constexpr bool contains(char32_t cp) const noexcept {
        return find(cp) != nullptr;
    }

    /**
     * @brief Returns the glyph drawn for @p cp.
     * @param cp The code point.
     * @return The font's glyph for @p cp, else the @ref replacement glyph
     *         (null if the font has none, meaning @p cp is skipped).
     */
    [[nodiscard]] constexpr const glyph_metrics* resolve(char32_t cp) const noexcept {
        const glyph_metrics* g = find(cp);
        return g != nullptr ? g : replacement;
    }

    /**
     * @brief Returns the horizontal distance the pen moves after @p cp.
     * @param cp The code point.
     * @return The advance of the glyph drawn for @p cp (see @ref resolve),
     *         or 0 if it is skipped.
     */
    [[nodiscard]] constexpr size_t advance(char32_t cp) const noexcept {
        const glyph_metrics* g = resolve(cp);
        return g != nullptr ? g->advance : 0;
    }

    /**
     * @brief Returns a pointer to the bitmap rows of @p g.
     * @param g A glyph of this font.
     * @return The first byte of the glyph's rows.
     */
    [[nodiscard]] constexpr const uint8_t* glyph_bitmap(const glyph_metrics& g) const noexcept {
        return bitmap + g.bitmap_offset;
    }

    /**
     * @brief Returns true if @p g has ink at (@p col, @p row) of its bitmap.
     * @param g   A glyph of this font.
     * @param col Column within the glyph bitmap, in `[0, g.width)`.
     * @param row Row within the glyph bitmap, in `[0, g.height)`.
     * @return true if the pixel is inked.
     */
    [[nodiscard]] constexpr bool
    ink_at(const glyph_metrics& g, size_t col, size_t row) const noexcept {
        return glyph_bitmap(g)[row * g.bytes_per_row() + col / 8] & (0x80u >> (col % 8));
    }
};

/**
 * @brief Returns the width in pixels of @p text rendered in @p font.
 *
 * Decodes @p text as UTF-8 (see @ref idfxx::utf8_view) and sums the advance of
 * every code point (see @ref proportional_font::advance). This is the
 * distance the pen travels, the width to use when centering, aligning or
 * wrapping text; a glyph's ink may overhang it slightly at either end.
 *
 * @param font The font.
 * @param text The UTF-8 text to measure.
 * @return The rendered width in pixels.
 */
[[nodiscard]] constexpr size_t
text_width(const proportional_font& font, std::string_view text) noexcept {
    size_t width = 0;
    for (char32_t cp : utf8_view(text)) {
        width += font.advance(cp);
    }
    return width;
}

} // namespace idfxx::font

/** @} */ // end of idfxx_font
