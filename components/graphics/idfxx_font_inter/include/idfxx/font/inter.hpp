// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/font/inter>
 * @file inter.hpp
 * @brief The Inter typeface as proportional bitmap fonts.
 *
 * @defgroup idfxx_font_inter Inter Fonts
 * @ingroup idfxx_font
 * @brief Inter Regular and Medium at 16, 24, 32 and 48 pixels.
 *
 * [Inter](https://rsms.me/inter/) is a sans-serif typeface designed for
 * screens, by The Inter Project Authors (SIL Open Font License 1.1). The
 * bundled fonts are Inter 4.1 Regular and Medium, rasterized to 1-bit glyphs
 * at each size, so they are drawn at their native size rather than scaled.
 * Each covers Windows-1252: printable ASCII, Latin-1 (such as accented
 * letters, `©` and `°`) and the typographic punctuation that Windows-1252 adds
 * (such as curly quotes, dashes, ellipsis, bullet, `€` and `™`). Other code
 * points are drawn as `?`.
 *
 * Sizes are em sizes. Capitals are roughly three quarters of the size tall.
 * Medium is the heavier weight. At 16 pixels, its stems are 2 pixels wide,
 * and Regular's stems are 1 pixel wide. So Medium is easier to read as small
 * text on low-resolution or ePaper displays. Each font is in its own
 * translation unit, so a size that an application never references is
 * dropped at link time.
 *
 * @code
 * const auto& f = idfxx::font::inter_24;
 * const size_t w = idfxx::font::text_width(f, "Crème brûlée");
 * canvas.draw_text(f, (canvas.width() - w) / 2, 40, "Crème brûlée", white);
 * canvas.draw_text(f, 8, 40 + f.line_height, "“naïve” café", white);
 * @endcode
 * @{
 */

#include <idfxx/font.hpp>

/**
 * @headerfile <idfxx/font/inter>
 * @brief Bitmap font types, text metrics, and bundled fonts.
 */
namespace idfxx::font {

/// Inter Regular, 16 px, for body text, with a 20-pixel line height.
extern const proportional_font inter_16;

/// Inter Regular, 24 px, for subheadings, with a 30-pixel line height.
extern const proportional_font inter_24;

/// Inter Regular, 32 px, for headings, with a 39-pixel line height.
extern const proportional_font inter_32;

/// Inter Regular, 48 px, for large headings and readouts, with a 59-pixel line height.
extern const proportional_font inter_48;

/// Inter Medium, 16 px, for body text, with a 20-pixel line height.
extern const proportional_font inter_medium_16;

/// Inter Medium, 24 px, for subheadings, with a 30-pixel line height.
extern const proportional_font inter_medium_24;

/// Inter Medium, 32 px, for headings, with a 39-pixel line height.
extern const proportional_font inter_medium_32;

/// Inter Medium, 48 px, for large headings and readouts, with a 59-pixel line height.
extern const proportional_font inter_medium_48;

} // namespace idfxx::font

/** @} */ // end of idfxx_font_inter
