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
#include <idfxx/required>

#include <cstdint>
#include <functional>

struct esp_lcd_touch_s;
typedef struct esp_lcd_touch_s esp_lcd_touch_t;
typedef esp_lcd_touch_t* esp_lcd_touch_handle_t;

/**
 * @defgroup idfxx_lcd_touch LCD Touch Component
 * @brief Touch controller interface and drivers for LCD panels.
 * @{
 */

/**
 * @headerfile <idfxx/lcd/touch>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/touch>
 * @brief Abstract base class for touch controllers.
 *
 * The public interface is non-virtual. Concrete drivers (e.g.
 * `idfxx::lcd::stmpe610`) customize behavior by overriding the protected
 * `do_*` hooks. This is the non-virtual-interface pattern that the standard
 * library also uses (for example, `std::pmr::memory_resource`).
 */
class touch {
public:
    /**
     * @headerfile <idfxx/lcd/touch>
     * @brief Callback type for processing touch coordinates.
     *
     * When exceptions are enabled, an exception thrown by the callback is
     * logged, and that read reports no touch points.
     */
    typedef std::move_only_function<void(
        uint16_t* x,
        uint16_t* y,
        uint16_t* strength,
        uint8_t* point_num,
        uint8_t max_point_num
    )>
        process_coordinates_callback;

    /**
     * @headerfile <idfxx/lcd/touch>
     * @brief Configuration structure for touch controllers.
     *
     * Every field other than x_max and y_max has a sensible default.
     */
    struct config {
        required<uint16_t> x_max; ///< Maximum X coordinate (for mirroring)
        required<uint16_t> y_max; ///< Maximum Y coordinate (for mirroring)

        gpio rst_gpio = gpio::nc(); ///< GPIO number of reset pin
        gpio int_gpio = gpio::nc(); ///< GPIO number of interrupt pin

        gpio::level reset_level = gpio::level::low;     ///< Level of the reset pin during reset
        gpio::level interrupt_level = gpio::level::low; ///< Active level of interrupt pin

        bool swap_xy = false;  ///< Swap X and Y after reading coordinates
        bool mirror_x = false; ///< Mirror X after reading coordinates
        bool mirror_y = false; ///< Mirror Y after reading coordinates

        /// Callback to apply user adjustments after reading coordinates from the touch controller,
        /// before swap/mirror
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

/** @} */ // end of idfxx_lcd_touch
