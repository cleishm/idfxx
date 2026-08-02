// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/ssd1680>
 * @file ssd1680.hpp
 * @brief SSD1680 ePaper panel driver.
 * @ingroup idfxx_epaper
 */

#include <idfxx/epaper/panel>
#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/memory>
#include <idfxx/panel_io>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <frequency/frequency>
#include <initializer_list>
#include <span>
#include <vector>

/**
 * @headerfile <idfxx/epaper/ssd1680>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/ssd1680>
 * @brief SSD1680 ePaper display controller driver.
 *
 * Driver for SSD1680-based SPI ePaper panels (up to 176x296), such as the
 * 2.13" 122x250 panel on the Seeed Studio XIAO ePaper Display Board EE05.
 * Implements the full @ref idfxx::epaper::panel interface: full, fast, and
 * partial refreshes, 4-level grayscale, deep sleep, and wake.
 *
 * The driver owns the panel's BUSY input and (optional) reset output lines,
 * and communicates through an `idfxx::panel_io` configured with
 * @ref spi_io_config. It keeps a shadow copy of the last-written frame
 * (`(width + 7) / 8 * height` bytes of DRAM, ~4 KB for the 2.13" panel) to
 * maintain the controller's previous-image plane for flicker-free partial
 * refreshes.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior. The destructor puts an awake controller into deep sleep.
 *
 * @code
 * idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);
 * idfxx::panel_io io(bus, idfxx::epaper::ssd1680::spi_io_config(PIN_CS, PIN_DC));
 * idfxx::epaper::ssd1680 display(io, {.reset_gpio = PIN_RST, .busy_gpio = PIN_BUSY});
 *
 * idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, true);
 * fb.flush(display);
 * display.refresh();
 * display.sleep();
 * @endcode
 */
class ssd1680 final : public panel {
public:
    /**
     * @headerfile <idfxx/epaper/ssd1680>
     * @brief Configuration structure for SSD1680 panels.
     */
    struct config {
        /// Panel width in pixels (sources). The controller drives up to 176.
        /// Defaults match the Seeed Studio 2.13" 122x250 panel.
        size_t width = 122;
        /// Panel height in pixels (gates). The controller drives up to 296.
        size_t height = 250;
        /// GPIO wired to the panel's reset line, or `gpio::nc()` if not
        /// wired. Required for @ref wake (the controller needs a hardware
        /// reset to leave deep sleep).
        gpio reset_gpio = gpio::nc();
        /// GPIO wired to the panel's BUSY output (required). The SSD1680
        /// drives BUSY high while an operation is in progress.
        gpio busy_gpio = gpio::nc();
        /// Maximum time to wait for the BUSY line to release. Full refreshes
        /// take several seconds on some panels and temperatures.
        std::chrono::milliseconds busy_timeout{15'000};
        /// Mirror the image horizontally (reverses the source scan via the
        /// controller's address decrement mode). Verify on hardware: some
        /// modules wire the source outputs such that mirroring shifts the
        /// image by the row-padding bits.
        bool mirror_x = false;
        /// Mirror the image vertically (reverses the gate scan via the
        /// controller's address decrement mode).
        bool mirror_y = false;
    };

    /**
     * @brief Returns a panel I/O configuration for communicating with an SSD1680 over SPI.
     *
     * Fills in the SPI framing the SSD1680 controller requires (mode 0,
     * 8-bit commands and parameters, a dedicated D/C line), so only the
     * wiring-specific values are parameters.
     *
     * @param cs   GPIO wired to the panel's chip-select line.
     * @param dc   GPIO wired to the panel's data/command line.
     * @param pclk SPI clock frequency. The SSD1680 supports up to 20 MHz
     *             for writes; 10 MHz is a conservative default.
     *
     * @return A panel_io::spi_config ready to construct a panel_io.
     *
     * @code
     * idfxx::panel_io io(bus, idfxx::epaper::ssd1680::spi_io_config(PIN_CS, PIN_DC));
     * idfxx::epaper::ssd1680 display(io, {.busy_gpio = PIN_BUSY});
     * @endcode
     */
    [[nodiscard]] static panel_io::spi_config
    spi_io_config(gpio cs, gpio dc, freq::hertz pclk = freq::hertz{10'000'000}) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new SSD1680 panel driver.
     *
     * Configures the BUSY and reset GPIOs, hardware-resets the controller,
     * and runs the full initialization sequence, leaving the panel awake in
     * @ref color_mode::mono and ready for writes.
     *
     * Does not take ownership of @p panel_io. It is the caller's
     * responsibility to ensure that this panel does not outlive the panel
     * I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   Panel configuration; `busy_gpio` is required.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg`
     *         for out-of-range dimensions or an unconnected `busy_gpio`.
     */
    [[nodiscard]] explicit ssd1680(idfxx::panel_io& panel_io, config config);
#endif

    /**
     * @brief Creates a new SSD1680 panel driver.
     *
     * Configures the BUSY and reset GPIOs, hardware-resets the controller,
     * and runs the full initialization sequence, leaving the panel awake in
     * @ref color_mode::mono and ready for writes.
     *
     * Does not take ownership of @p panel_io. It is the caller's
     * responsibility to ensure that this panel does not outlive the panel
     * I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   Panel configuration; `busy_gpio` is required.
     *
     * @return The new ssd1680, or an error.
     * @retval idfxx::errc::invalid_arg if `busy_gpio` is not connected, or
     *         the dimensions are zero or exceed the controller's 176x296.
     * @retval idfxx::errc::timeout if the controller's BUSY line does not
     *         release during initialization.
     */
    [[nodiscard]] static result<ssd1680> make(idfxx::panel_io& panel_io, config config);

    ~ssd1680() override;

    ssd1680(const ssd1680&) = delete;
    ssd1680& operator=(const ssd1680&) = delete;
    ssd1680(ssd1680&& other) noexcept;
    ssd1680& operator=(ssd1680&& other) noexcept;

private:
    // A rectangular pixel region of the panel, for RAM window programming.
    struct region {
        size_t x;
        size_t y;
        size_t width;
        size_t height;
    };

    explicit ssd1680(idfxx::panel_io& panel_io, config config, std::vector<uint8_t, dram_allocator<uint8_t>> shadow)
        : panel(
              config.width,
              config.height,
              config.busy_gpio,
              gpio::level::high,
              config.busy_timeout,
              config.reset_gpio
          )
        , _io(&panel_io)
        , _mirror_x(config.mirror_x)
        , _mirror_y(config.mirror_y)
        , _shadow(std::move(shadow)) {}

    // epaper::panel customization hooks.
    [[nodiscard]] result<void>
    do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override;
    [[nodiscard]] result<void>
    do_write(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override;
    [[nodiscard]] result<void> do_clear() override;
    [[nodiscard]] result<void> do_refresh(refresh_mode mode) override;
    [[nodiscard]] result<void> do_set_color_mode(enum color_mode mode) override;
    [[nodiscard]] result<void> do_sleep() override;
    [[nodiscard]] result<void> do_wake() override;

    // Command sequencing helpers (see src/ssd1680.cpp).
    [[nodiscard]] result<void> _cmd(uint8_t cmd);
    [[nodiscard]] result<void> _cmd(uint8_t cmd, std::initializer_list<uint8_t> params);
    [[nodiscard]] result<void> _cmd(uint8_t cmd, std::span<const uint8_t> params);
    [[nodiscard]] result<void> _stream(uint8_t cmd, std::span<const uint8_t> data);
    [[nodiscard]] result<void> _drain();
    [[nodiscard]] result<void> _init();
    [[nodiscard]] result<void> _init_gray();
    [[nodiscard]] result<void> _init_scan();
    [[nodiscard]] result<void> _set_ram_window(region r);
    [[nodiscard]] result<void>
    _write_planes(region window, std::span<const uint8_t> new_plane, std::span<const uint8_t> old_plane);
    [[nodiscard]] result<void> _sync_old_plane();
    [[nodiscard]] size_t _stride() const noexcept { return (width() + 7) / 8; }

    idfxx::panel_io* _io = nullptr; // nullptr after move
    bool _mirror_x = false;
    bool _mirror_y = false;
    // Shadow of the last-written new-plane RAM bytes; re-sent to the
    // controller's old-image plane after each refresh so partial updates
    // diff against the frame actually on the glass.
    std::vector<uint8_t, dram_allocator<uint8_t>> _shadow;
};

} // namespace idfxx::epaper
