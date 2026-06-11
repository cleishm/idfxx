// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/ili9341>
 * @file ili9341.hpp
 * @brief ILI9341 LCD panel driver.
 * @ingroup idfxx_lcd
 */

#include <idfxx/lcd/panel>
#include <idfxx/lcd/panel_io>

/**
 * @headerfile <idfxx/lcd/ili9341>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/ili9341>
 * @brief ILI9341 display controller driver.
 *
 * Driver for ILI9341-based LCD displays (typically 240x320 resolution).
 */
class ili9341 : public panel {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new ili9341 panel.
     *
     * Does not take ownership of @p panel_io. It is the caller's responsibility to ensure that
     * this panel does not outlive the panel I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   panel configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] explicit ili9341(idfxx::lcd::panel_io& panel_io, panel::config config);
#endif

    /**
     * @brief Creates a new ili9341 panel.
     *
     * Does not take ownership of @p panel_io. It is the caller's responsibility to ensure that
     * this panel does not outlive the panel I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   panel configuration.
     *
     * @return The new ili9341, or an error.
     */
    [[nodiscard]] static result<ili9341> make(idfxx::lcd::panel_io& panel_io, panel::config config);

    ~ili9341();

    ili9341(const ili9341&) = delete;
    ili9341& operator=(const ili9341&) = delete;
    ili9341(ili9341&& other) noexcept;
    ili9341& operator=(ili9341&& other) noexcept;

private:
    ili9341() = default;
    explicit ili9341(esp_lcd_panel_handle_t handle)
        : _handle(handle) {}

    // lcd::panel customization hooks.
    [[nodiscard]] esp_lcd_panel_handle_t do_idf_handle() const override;
    [[nodiscard]] result<void> do_swap_xy(bool swap) override;
    [[nodiscard]] result<void> do_mirror(bool mirror_x, bool mirror_y) override;
    [[nodiscard]] result<void> do_display_on(bool on) override;

    esp_lcd_panel_handle_t _handle = nullptr;
};

} // namespace idfxx::lcd
