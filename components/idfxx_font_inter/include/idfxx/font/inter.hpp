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
 * @brief Inter Regular at 16, 24, 32 and 48 pixels.
 *
 * [Inter](https://rsms.me/inter/) is a sans-serif typeface designed for
 * screens, by The Inter Project Authors (SIL Open Font License 1.1). The
 * bundled fonts are Inter 4.1 Regular, rasterized to 1-bit glyphs at each
 * size, so they are drawn at their native size rather than scaled. Each
 * covers Windows-1252: printable ASCII, Latin-1 (accented letters, `©`,
 * `°`, ...) and the typographic punctuation Windows-1252 adds (curly quotes,
 * dashes, ellipsis, bullet, `€`, `™`, ...). Other code points are drawn as
 * `?`.
 *
 * Sizes are em sizes; capitals are roughly three quarters of the size tall.
 * Each font lives in its own translation unit, so a size an application
 * never references is dropped at link time.
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

/// Inter Regular, 16 px — body text; 20-pixel line height.
extern const proportional_font inter_16;

/// Inter Regular, 24 px — subheadings; 30-pixel line height.
extern const proportional_font inter_24;

/// Inter Regular, 32 px — headings; 39-pixel line height.
extern const proportional_font inter_32;

/// Inter Regular, 48 px — large headings and readouts; 59-pixel line height.
extern const proportional_font inter_48;

} // namespace idfxx::font

/** @} */ // end of idfxx_font_inter
