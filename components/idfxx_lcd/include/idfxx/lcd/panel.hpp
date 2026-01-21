// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/panel>
 * @file panel.hpp
 * @brief Abstract LCD panel interface.
 * @ingroup idfxx_lcd
 */

#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/lcd/color>

struct esp_lcd_panel_t;
typedef struct esp_lcd_panel_t* esp_lcd_panel_handle_t;

/**
 * @headerfile <idfxx/lcd/panel>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/panel>
 * @brief Abstract base class for LCD panels.
 */
class panel {
public:
    /**
     * @headerfile <idfxx/lcd/panel>
     * @brief Configuration structure for LCD panels.
     */
    struct config {
        gpio reset_gpio = gpio::nc();             ///< GPIO number for hardware reset, or idfxx::gpio::nc() if not used
        enum rgb_element_order rgb_element_order; ///< Set RGB element order, RGB or BGR
        rgb_data_endian data_endian = rgb_data_endian::big; ///< Set the data endian for color data larger than 1 byte
        uint32_t bits_per_pixel;                            ///< Color depth, in bpp
        struct {
            unsigned int reset_active_high : 1 = 0; ///< Setting this if the panel reset is high level active
        } flags = {};                               ///< LCD panel config flags
        void* vendor_config = nullptr; ///< vendor specific configuration, optional, left as NULL if not used
    };

    virtual ~panel() = default;

    /** @brief Returns the underlying ESP-IDF handle. */
    [[nodiscard]] virtual esp_lcd_panel_handle_t idf_handle() const = 0;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Swaps the X and Y axes.
     * @param swap true to swap, false for normal.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    virtual void swap_xy(bool swap) { unwrap(try_swap_xy(swap)); }
#endif

    /**
     * @brief Swaps the X and Y axes.
     * @param swap true to swap, false for normal.
     * @return Success, or an error.
     */
    [[nodiscard]] virtual result<void> try_swap_xy(bool swap) = 0;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Mirrors the display.
     * @param mirrorX true to mirror horizontally.
     * @param mirrorY true to mirror vertically.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    virtual void mirror(bool mirrorX, bool mirrorY) { unwrap(try_mirror(mirrorX, mirrorY)); }
#endif

    /**
     * @brief Mirrors the display.
     * @param mirrorX true to mirror horizontally.
     * @param mirrorY true to mirror vertically.
     * @return Success, or an error.
     */
    [[nodiscard]] virtual result<void> try_mirror(bool mirrorX, bool mirrorY) = 0;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Turns the display on or off.
     * @param on true to turn on, false to turn off.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    virtual void display_on(bool on) { unwrap(try_display_on(on)); }
#endif

    /**
     * @brief Turns the display on or off.
     * @param on true to turn on, false to turn off.
     * @return Success, or an error.
     */
    [[nodiscard]] virtual result<void> try_display_on(bool on) = 0;
};

} // namespace idfxx::lcd
