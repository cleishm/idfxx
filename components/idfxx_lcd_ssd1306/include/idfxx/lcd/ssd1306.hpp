// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/ssd1306>
 * @file ssd1306.hpp
 * @brief SSD1306 OLED panel driver.
 * @ingroup idfxx_lcd
 */

#include <idfxx/lcd/panel>
#include <idfxx/lcd/panel_io>

#include <cstddef>

/**
 * @headerfile <idfxx/lcd/ssd1306>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/ssd1306>
 * @brief SSD1306 monochrome OLED display controller driver.
 *
 * Driver for SSD1306-based OLED displays (128x64 or 128x32, 1 bit per pixel).
 * The display starts off; call display_on(true) after construction to make
 * output visible.
 *
 * Pixel data for draw_bitmap must be page-packed: each byte holds 8
 * vertically adjacent pixels (bit 0 topmost), and pages (rows of 8 pixels)
 * are laid out top-to-bottom. Row coordinates passed to draw_bitmap must be
 * page-aligned (multiples of 8). `idfxx::lcd::mono_framebuffer` produces this
 * layout directly.
 */
class ssd1306 : public panel {
public:
    /**
     * @headerfile <idfxx/lcd/ssd1306>
     * @brief Configuration structure for SSD1306 panels.
     */
    struct config {
        gpio reset_gpio = gpio::nc(); ///< GPIO number for hardware reset, or idfxx::gpio::nc() if not used
        uint32_t height = 64;         ///< Display height in pixels (64 or 32)
        gpio::level reset_active_level = gpio::level::low; ///< Active level for the panel reset signal
    };

    /**
     * @brief Returns a panel I/O configuration for communicating with an SSD1306 over I2C.
     *
     * Fills in the I2C framing the SSD1306 controller requires (a one-byte control
     * phase with the D/C selection in bit 6, and 8-bit commands and parameters), so
     * only the wiring-specific values are parameters.
     *
     * @param device_address The panel's 7-bit I2C address. Most modules use 0x3C;
     *                       some are strapped to 0x3D.
     * @param scl_speed      The I2C SCL frequency. The SSD1306 supports up to 400 kHz.
     *
     * @return A panel_io::i2c_config ready to construct a panel_io.
     *
     * @code
     * idfxx::lcd::panel_io io(bus, idfxx::lcd::ssd1306::i2c_io_config());
     * idfxx::lcd::ssd1306 display(io, {.height = 64});
     * @endcode
     */
    [[nodiscard]] static panel_io::i2c_config
    i2c_io_config(uint16_t device_address = 0x3C, freq::hertz scl_speed = freq::hertz{400000}) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new ssd1306 panel.
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
    [[nodiscard]] explicit ssd1306(idfxx::lcd::panel_io& panel_io, config config);
#endif

    /**
     * @brief Creates a new ssd1306 panel.
     *
     * Does not take ownership of @p panel_io. It is the caller's responsibility to ensure that
     * this panel does not outlive the panel I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   panel configuration.
     *
     * @return The new ssd1306, or an error.
     * @retval idfxx::errc::invalid_arg if the configured height is not 64 or 32.
     */
    [[nodiscard]] static result<ssd1306> make(idfxx::lcd::panel_io& panel_io, config config);

    ~ssd1306();

    ssd1306(const ssd1306&) = delete;
    ssd1306& operator=(const ssd1306&) = delete;
    ssd1306(ssd1306&& other) noexcept;
    ssd1306& operator=(ssd1306&& other) noexcept;

    /** @brief Returns the display width in pixels (always 128). */
    [[nodiscard]] size_t width() const noexcept { return 128; }

    /** @brief Returns the display height in pixels (64 or 32, as configured). */
    [[nodiscard]] size_t height() const noexcept { return _height; }

private:
    explicit ssd1306(esp_lcd_panel_handle_t handle, size_t height)
        : _handle(handle)
        , _height(height) {}

    // lcd::panel customization hooks.
    [[nodiscard]] esp_lcd_panel_handle_t do_idf_handle() const override;

    esp_lcd_panel_handle_t _handle = nullptr;
    size_t _height = 0;
};

} // namespace idfxx::lcd
