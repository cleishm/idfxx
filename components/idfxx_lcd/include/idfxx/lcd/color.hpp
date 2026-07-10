// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/color>
 * @file color.hpp
 * @brief LCD color types.
 * @ingroup idfxx_lcd
 */

#include <bit>
#include <cstdint>
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

/**
 * @headerfile <idfxx/lcd/color>
 * @brief A 16-bit RGB565 color, stored in panel byte order.
 *
 * The stored representation places the most significant byte first, the
 * order color panels expect by default (@ref rgb_data_endian::big), so
 * arrays of rgb565 values can be passed directly to
 * @ref panel::draw_bitmap without conversion.
 *
 * @code
 * constexpr idfxx::lcd::rgb565 amber(255, 191, 0);
 * fb.set_pixel(10, 20, amber);
 * @endcode
 */
class rgb565 {
public:
    /** @brief Creates black. */
    constexpr rgb565() noexcept = default;

    /**
     * @brief Creates a color from 8-bit red, green, and blue components.
     *
     * Components are truncated to the 5-6-5 bit layout: red and blue keep
     * their top 5 bits, green its top 6.
     *
     * @param r Red component, 0-255.
     * @param g Green component, 0-255.
     * @param b Blue component, 0-255.
     */
    constexpr rgb565(uint8_t r, uint8_t g, uint8_t b) noexcept
        : _repr(_to_panel_order(static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3)))) {}

    /**
     * @brief Creates a color from a packed RGB565 value.
     *
     * @param value Packed color: red in bits 15-11, green in bits 10-5, blue
     *              in bits 4-0.
     * @return The color.
     */
    [[nodiscard]] static constexpr rgb565 from_value(uint16_t value) noexcept {
        rgb565 c;
        c._repr = _to_panel_order(value);
        return c;
    }

    /**
     * @brief Returns the packed RGB565 value.
     * @return The packed color: red in bits 15-11, green in bits 10-5, blue
     *         in bits 4-0.
     */
    [[nodiscard]] constexpr uint16_t value() const noexcept { return _to_panel_order(_repr); }

    /** @brief Compares two colors for equality. */
    friend constexpr bool operator==(rgb565, rgb565) noexcept = default;

private:
    // Byte-swaps a packed value into big-endian panel order (an involution,
    // so it converts back as well).
    static constexpr uint16_t _to_panel_order(uint16_t value) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            return std::byteswap(value);
        } else {
            return value;
        }
    }

    uint16_t _repr = 0;
};

static_assert(sizeof(rgb565) == 2);

} // namespace idfxx::lcd
