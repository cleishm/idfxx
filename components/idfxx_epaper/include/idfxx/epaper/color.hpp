// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/color>
 * @file color.hpp
 * @brief Pixel value types for ePaper displays.
 * @ingroup idfxx_epaper
 */

#include <cstdint>

/**
 * @headerfile <idfxx/epaper/color>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/color>
 * @brief A 4-level grayscale pixel value.
 *
 * The value type written by @ref gray4_framebuffer::set_pixel. Levels run
 * from white paper to full black ink; a value-initialized `gray4{}` is white,
 * so generic drawing code that clears with a value-initialized pixel (e.g.
 * `idfxx::gfx::canvas::clear`) clears ePaper surfaces to blank paper.
 */
enum class gray4 : uint8_t {
    white = 0, ///< Blank paper (no ink).
    light = 1, ///< Light gray.
    dark = 2,  ///< Dark gray.
    black = 3, ///< Full black ink.
};

/**
 * @headerfile <idfxx/epaper/color>
 * @brief A six-color (E Ink Spectra 6) pixel value.
 *
 * The value type written by @ref color6_framebuffer::set_pixel. The
 * enumerators are the panel-native 4-bit pixel codes, so a framebuffer's
 * bytes stream to the controller unchanged. Note that, unlike @ref gray4, a
 * value-initialized `color6{}` is **black** (code 0), not white: generic
 * drawing code that clears with a value-initialized pixel (e.g. a windowed
 * `idfxx::gfx::canvas::clear`) inks the region black. Use
 * @ref color6_framebuffer::clear (or `fill(color6::white)`) to blank a whole
 * frame to paper.
 */
enum class color6 : uint8_t {
    black = 0x0,  ///< Black ink.
    white = 0x1,  ///< Blank paper.
    yellow = 0x2, ///< Yellow.
    red = 0x3,    ///< Red.
    blue = 0x5,   ///< Blue.
    green = 0x6,  ///< Green.
};

} // namespace idfxx::epaper
