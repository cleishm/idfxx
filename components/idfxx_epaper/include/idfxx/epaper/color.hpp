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

} // namespace idfxx::epaper
