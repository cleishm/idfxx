// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/color>
 * @file color.hpp
 * @brief LCD color types.
 * @ingroup idfxx_lcd
 */

#include <esp_lcd_panel_io.h>

/**
 * @headerfile <idfxx/lcd/color>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/color>
 * @brief RGB element order for LCD panels.
 */
enum class rgb_element_order : int {
    rgb = LCD_RGB_ELEMENT_ORDER_RGB, ///< RGB element order
    bgr = LCD_RGB_ELEMENT_ORDER_BGR, ///< BGR element order
};

/**
 * @headerfile <idfxx/lcd/color>
 * @brief RGB data endian for LCD panels.
 */
enum class rgb_data_endian : int {
    big = LCD_RGB_DATA_ENDIAN_BIG,       ///< RGB data endian: MSB first
    little = LCD_RGB_DATA_ENDIAN_LITTLE, ///< RGB data endian: LSB first
};

} // namespace idfxx::lcd
