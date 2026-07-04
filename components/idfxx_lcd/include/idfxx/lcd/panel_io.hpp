// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/panel_io>
 * @file panel_io.hpp
 * @brief LCD panel I/O class.
 *
 * @defgroup idfxx_lcd LCD Component
 * @brief LCD panel I/O interface for SPI- and I2C-based displays.
 *
 * Provides the communication layer for SPI- and I2C-based LCD panels and
 * touch controllers.
 *
 * Depends on @ref idfxx_core for error handling, @ref idfxx_gpio for pin
 * configuration, @ref idfxx_spi for SPI bus integration, and @ref idfxx_i2c
 * for I2C bus integration.
 * @{
 */

#include <idfxx/i2c/master>
#include <idfxx/spi/master>

#include <esp_lcd_panel_io.h>
#include <frequency/frequency>
#include <functional>
#include <memory>

/**
 * @headerfile <idfxx/lcd/panel_io>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/panel_io>
 * @brief Panel I/O interface for SPI- and I2C-connected displays.
 *
 * Provides the communication layer for LCD panels and touch controllers
 * that use SPI or I2C. This type is non-copyable and move-only. A moved-from
 * object must not be used: any operation other than destruction or
 * assignment is undefined behavior.
 */
class panel_io {
public:
    /**
     * @brief Callback type invoked when color data transfer has finished.
     *
     * @param edata Panel IO event data.
     * @return Whether a high priority task has been woken up by this function.
     */
    typedef std::move_only_function<bool(esp_lcd_panel_io_event_data_t* edata)> color_transfer_done_callback;

    /**
     * @brief SPI-based panel I/O configuration.
     *
     * All fields have sensible defaults, but you must define cs_gpio, pclk_freq, lcd_cmd_bits,
     * and lcd_param_bits at minimum.
     */
    struct spi_config {
        gpio cs_gpio = gpio::nc();    ///< GPIO used for CS line
        gpio dc_gpio = gpio::nc();    ///< GPIO used to select the D/C line, set to gpio::nc() if not used
        int spi_mode = 0;             ///< Traditional SPI mode (0~3)
        freq::hertz pclk_freq{0};     ///< Frequency of pixel clock
        size_t trans_queue_depth = 0; ///< Size of internal transaction queue
        color_transfer_done_callback on_color_transfer_done =
            nullptr;            ///< Callback invoked when color data transfer has finished
        int lcd_cmd_bits = 0;   ///< Bit-width of LCD command
        int lcd_param_bits = 0; ///< Bit-width of LCD parameter
        uint8_t cs_enable_pretrans =
            0; ///< Amount of SPI bit-cycles the cs should be activated before the transmission (0-16)
        uint8_t cs_enable_posttrans =
            0; ///< Amount of SPI bit-cycles the cs should stay active after the transmission (0-16)
        struct {
            unsigned int dc_high_on_cmd : 1 = 0;  ///< If enabled, DC level = 1 indicates command transfer
            unsigned int dc_low_on_data : 1 = 0;  ///< If enabled, DC level = 0 indicates color data transfer
            unsigned int dc_low_on_param : 1 = 0; ///< If enabled, DC level = 0 indicates parameter transfer
            unsigned int octal_mode : 1 =
                0; ///< transmit with octal mode (8 data lines), this mode is used to simulate Intel 8080 timing
            unsigned int quad_mode : 1 = 0;      ///< transmit with quad mode (4 data lines), this mode is useful when
                                                 ///< transmitting LCD parameters (Only use one line for command)
            unsigned int sio_mode : 1 = 0;       ///< Read and write through a single data line (MOSI)
            unsigned int lsb_first : 1 = 0;      ///< transmit LSB bit first
            unsigned int cs_high_active : 1 = 0; ///< CS line is high active
        } flags = {};                            ///< Extra flags to fine-tune the SPI device
    };

    /**
     * @brief I2C-based panel I/O configuration.
     *
     * All fields have sensible defaults, but you must define device_address, scl_speed,
     * lcd_cmd_bits, and lcd_param_bits at minimum. The control-phase framing is a property
     * of the panel controller; panel driver components typically provide a pre-filled
     * configuration (e.g. `ssd1306::i2c_io_config()`) or document the values they require.
     */
    struct i2c_config {
        uint16_t device_address = 0; ///< 7-bit I2C device address (e.g. 0x3C for a typical SSD1306)
        freq::hertz scl_speed{0};    ///< I2C SCL frequency
        color_transfer_done_callback on_color_transfer_done =
            nullptr;                    ///< Callback invoked when color data transfer has finished
        size_t control_phase_bytes = 0; ///< Number of bytes the panel encodes control information (e.g. D/C
                                        ///< selection) into during the control phase
        unsigned int dc_bit_offset = 0; ///< Offset of the D/C selection bit in the control phase
        int lcd_cmd_bits = 0;           ///< Bit-width of LCD command
        int lcd_param_bits = 0;         ///< Bit-width of LCD parameter
        struct {
            unsigned int dc_low_on_data : 1 = 0; ///< If enabled, DC bit = 0 indicates data transfer and DC bit = 1
                                                 ///< indicates command transfer; vice versa otherwise
            unsigned int disable_control_phase : 1 = 0; ///< If enabled, the control phase isn't used
        } flags = {};                                   ///< Extra flags to fine-tune the I2C device
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new panel I/O interface.
     *
     * Does not take ownership of @p spi_bus. It is the caller's responsibility to ensure that
     * this panel_io does not outlive the bus.
     *
     * @param spi_bus The SPI bus.
     * @param config  Panel I/O configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit panel_io(idfxx::spi::master_bus& spi_bus, spi_config config);
#endif

    /**
     * @brief Creates a new panel I/O interface.
     *
     * Does not take ownership of @p spi_bus. It is the caller's responsibility to ensure that
     * this panel_io does not outlive the bus.
     *
     * @param spi_bus The SPI bus.
     * @param config  Panel I/O configuration.
     *
     * @return The new panel_io, or an error.
     */
    [[nodiscard]] static result<panel_io> make(idfxx::spi::master_bus& spi_bus, spi_config config);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new panel I/O interface.
     *
     * Does not take ownership of @p i2c_bus. It is the caller's responsibility to ensure that
     * this panel_io does not outlive the bus.
     *
     * @param i2c_bus The I2C bus.
     * @param config  Panel I/O configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit panel_io(idfxx::i2c::master_bus& i2c_bus, i2c_config config);
#endif

    /**
     * @brief Creates a new panel I/O interface.
     *
     * Does not take ownership of @p i2c_bus. It is the caller's responsibility to ensure that
     * this panel_io does not outlive the bus.
     *
     * @param i2c_bus The I2C bus.
     * @param config  Panel I/O configuration.
     *
     * @return The new panel_io, or an error.
     */
    [[nodiscard]] static result<panel_io> make(idfxx::i2c::master_bus& i2c_bus, i2c_config config);

    ~panel_io();

    panel_io(const panel_io&) = delete;
    panel_io& operator=(const panel_io&) = delete;
    panel_io(panel_io&& other) noexcept;
    panel_io& operator=(panel_io&& other) noexcept;

    /** @brief Returns the underlying ESP-IDF handle. */
    [[nodiscard]] esp_lcd_panel_io_handle_t idf_handle() const { return _handle; }

private:
    panel_io() = default;

    struct callback_state {
        color_transfer_done_callback on_color_transfer_done;
    };

    panel_io(esp_lcd_panel_io_handle_t handle, std::unique_ptr<callback_state> callbacks);

    static bool
    on_color_transfer_done(esp_lcd_panel_io_handle_t handle, esp_lcd_panel_io_event_data_t* edata, void* user_ctx);

    esp_lcd_panel_io_handle_t _handle = nullptr;
    std::unique_ptr<callback_state> _callbacks; // heap-stable state for ESP-IDF callbacks
};

/** @} */ // end of idfxx_lcd

} // namespace idfxx::lcd
