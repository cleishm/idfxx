// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/font/spleen>
 * @file spleen.hpp
 * @brief The Spleen bitmap fonts.
 *
 * @defgroup idfxx_font_spleen Spleen Fonts
 * @ingroup idfxx_font
 * @brief The Spleen 5x8 and 8x16 fixed-cell bitmap fonts.
 *
 * [Spleen](https://github.com/fcambus/spleen) is a monospaced bitmap font
 * family by Frederic Cambus (BSD-2-Clause). The bundled sizes cover
 * printable ASCII (0x20-0x7E). Each font lives in its own translation unit,
 * so a font an application never references is dropped at link time.
 * @{
 */

#include <idfxx/font.hpp>

/**
 * @headerfile <idfxx/font/spleen>
 * @brief Bitmap font types, text metrics, and bundled fonts.
 */
namespace idfxx::font {

/// Spleen 5x8 — compact status text.
extern const mono_font spleen_5x8;

/// Spleen 8x16 — headline text; scale 2 gives 16x32 digits.
extern const mono_font spleen_8x16;

/** @} */ // end of idfxx_font_spleen

} // namespace idfxx::font
