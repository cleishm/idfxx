// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/panel_io>
 * @file panel_io.hpp
 * @brief LCD panel I/O class.
 *
 * @defgroup idfxx_lcd LCD Component
 * @brief LCD panel I/O interface for SPI-based displays.
 *
 * Provides the communication layer for SPI-based LCD panels and touch
 * controllers.
 *
 * Depends on @ref idfxx_core for error handling, @ref idfxx_gpio for pin
 * configuration, and @ref idfxx_spi for SPI bus integration.
 * @{
 */

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
 * @brief SPI-based panel I/O interface.
 *
 * Provides the communication layer for LCD panels and touch controllers
 * that use SPI. This type is non-copyable and move-only. Result-returning
 * methods on a moved-from object return errc::invalid_state. Simple
 * accessors return default/null values.
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
