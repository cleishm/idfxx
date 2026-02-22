// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/stmpe610>
 * @file stmpe610.hpp
 * @brief STMPE610 resistive touch controller driver.
 * @ingroup idfxx_lcd_touch
 */

#include <idfxx/error>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/touch>

/**
 * @headerfile <idfxx/lcd/stmpe610>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/stmpe610>
 * @brief STMPE610 resistive touch controller driver.
 *
 * Driver for STMPE610-based resistive touch controllers, commonly paired
 * with ILI9341 displays.
 */
class stmpe610 : public touch {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new stmpe610 touch controller.
     *
     * Does not take ownership of @p panel_io. It is the caller's responsibility to ensure that
     * this controller does not outlive the panel I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   Configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] explicit stmpe610(idfxx::lcd::panel_io& panel_io, struct config config);
#endif

    /**
     * @brief Creates a new stmpe610 touch controller.
     *
     * Does not take ownership of @p panel_io. It is the caller's responsibility to ensure that
     * this controller does not outlive the panel I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   Configuration.
     *
     * @return The new stmpe610, or an error.
     */
    [[nodiscard]] static result<stmpe610> make(idfxx::lcd::panel_io& panel_io, struct config config);

    ~stmpe610();

    stmpe610(const stmpe610&) = delete;
    stmpe610& operator=(const stmpe610&) = delete;
    stmpe610(stmpe610&& other) noexcept;
    stmpe610& operator=(stmpe610&& other) noexcept;

    /** @copydoc touch::idf_handle() */
    [[nodiscard]] esp_lcd_touch_handle_t idf_handle() const override;

private:
    stmpe610() = default;

    result<esp_lcd_touch_handle_t> make_handle(esp_lcd_panel_io_handle_t io_handle, const config& config);
    static void process_coordinates(
        esp_lcd_touch_handle_t tp,
        uint16_t* x,
        uint16_t* y,
        uint16_t* strength,
        uint8_t* point_num,
        uint8_t max_point_num
    );

    struct callback_state {
        process_coordinates_callback process_coordinates;
    };

    esp_lcd_touch_handle_t _handle = nullptr;
    std::unique_ptr<callback_state> _callbacks; // heap-stable state for ESP-IDF callbacks
};

} // namespace idfxx::lcd
