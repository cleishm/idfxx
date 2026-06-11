// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/touch>
 * @file touch.hpp
 * @brief Abstract touch controller interface.
 * @ingroup idfxx_lcd_touch
 */

#include <idfxx/gpio>

#include <cstdint>
#include <functional>

struct esp_lcd_touch_s;
typedef struct esp_lcd_touch_s esp_lcd_touch_t;
typedef esp_lcd_touch_t* esp_lcd_touch_handle_t;

/**
 * @headerfile <idfxx/lcd/touch>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/touch>
 * @brief Abstract base class for touch controllers.
 *
 * The public interface is non-virtual; concrete drivers (e.g.
 * `idfxx::lcd::stmpe610`) customize behaviour by overriding the protected
 * `do_*` hooks, mirroring the standard library's non-virtual-interface
 * pattern (cf. `std::pmr::memory_resource`).
 */
class touch {
public:
    /**
     * @headerfile <idfxx/lcd/touch>
     * @brief Callback type for processing touch coordinates.
     */
    typedef std::move_only_function<
        void(uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_num, uint8_t max_point_num)>
        process_coordinates_callback;

    /**
     * @headerfile <idfxx/lcd/touch>
     * @brief Configuration structure for touch controllers.
     *
     * All fields have sensible defaults, but you must define x_max and y_max at minimum.
     */
    struct config {
        uint16_t x_max = 0; ///< X coordinates max (for mirroring)
        uint16_t y_max = 0; ///< Y coordinates max (for mirroring)

        gpio rst_gpio = gpio::nc(); ///< GPIO number of reset pin
        gpio int_gpio = gpio::nc(); ///< GPIO number of interrupt pin

        gpio::level reset_level = gpio::level::low;     ///< Level of reset pin in reset
        gpio::level interrupt_level = gpio::level::low; ///< Active level of interrupt pin

        bool swap_xy = false;  ///< Swap X and Y after read coordinates
        bool mirror_x = false; ///< Mirror X after read coordinates
        bool mirror_y = false; ///< Mirror Y after read coordinates

        /// Callback to apply user adjustments after reading coordinates from the touch controller
        process_coordinates_callback process_coordinates = nullptr;
    };

    virtual ~touch() = default;

    touch(const touch&) = delete;
    touch& operator=(const touch&) = delete;

    /**
     * @brief Returns the underlying ESP-IDF handle.
     * @return The ESP-IDF touch controller handle.
     */
    [[nodiscard]] esp_lcd_touch_handle_t idf_handle() const { return do_idf_handle(); }

protected:
    touch() = default;
    touch(touch&&) noexcept = default;
    touch& operator=(touch&&) noexcept = default;

    /// Hook for @ref idf_handle.
    [[nodiscard]] virtual esp_lcd_touch_handle_t do_idf_handle() const = 0;
};

} // namespace idfxx::lcd
