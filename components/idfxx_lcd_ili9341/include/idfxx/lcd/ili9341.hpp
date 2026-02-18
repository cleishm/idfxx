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

#include <memory>

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
    /**
     * @brief Creates a new ili9341 panel.
     *
     * @param panel_io The panel I/O interface.
     * @param config   panel configuration.
     *
     * @return The new ili9341, or an error.
     */
    [[nodiscard]] static result<std::unique_ptr<ili9341>>
    make(std::shared_ptr<idfxx::lcd::panel_io> panel_io, panel::config config);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new ili9341 panel.
     *
     * @param panel_io The panel I/O interface.
     * @param config   panel configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    [[nodiscard]] explicit ili9341(std::shared_ptr<idfxx::lcd::panel_io> panel_io, panel::config config);
#endif

    ~ili9341();

    ili9341(const ili9341&) = delete;
    ili9341& operator=(const ili9341&) = delete;
    ili9341(ili9341&&) = delete;
    ili9341& operator=(ili9341&&) = delete;

    [[nodiscard]] esp_lcd_panel_handle_t idf_handle() const override;
    [[nodiscard]] result<void> try_swap_xy(bool swap) override;
    [[nodiscard]] result<void> try_mirror(bool mirrorX, bool mirrorY) override;
    [[nodiscard]] result<void> try_display_on(bool on) override;

private:
    explicit ili9341() = default;

    result<esp_lcd_panel_handle_t> make_handle(esp_lcd_panel_io_handle_t io_handle, const panel::config& config);

    std::shared_ptr<idfxx::lcd::panel_io> _panel_io;
    esp_lcd_panel_handle_t _handle;
};

} // namespace idfxx::lcd
