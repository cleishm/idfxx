// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/uc8179>
 * @file uc8179.hpp
 * @brief UC8179 ePaper panel driver.
 * @ingroup idfxx_epaper
 */

#include <idfxx/epaper/panel>
#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/panel_io>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <frequency/frequency>
#include <initializer_list>
#include <span>

/**
 * @headerfile <idfxx/epaper/uc8179>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/uc8179>
 * @brief UC8179 ePaper display controller driver.
 *
 * Driver for UC8179-based SPI ePaper panels (up to 800x600), such as the
 * 7.5" 800x480 panel (GooDisplay GDEY075T7 glass) on the Seeed Studio XIAO
 * ePaper Display Board EE05. Implements the full @ref idfxx::epaper::panel
 * interface: full, fast, and partial refreshes, 4-level grayscale, deep
 * sleep, and wake.
 *
 * The driver owns the panel's BUSY input (active low on this controller)
 * and (optional) reset output lines, and communicates through an
 * `idfxx::panel_io` configured with @ref spi_io_config. Unlike
 * controllers that need the previous frame re-sent by the host, the UC8179
 * copies the new-image RAM to the previous-image RAM when a refresh
 * completes, so the driver keeps no shadow frame — full-frame writes stream
 * the framebuffer's 48 KB (at 800x480) straight to the controller.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior. The destructor powers off an awake controller and puts it into
 * deep sleep.
 *
 * @code
 * idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);
 * idfxx::panel_io io(bus, idfxx::epaper::uc8179::spi_io_config(PIN_CS, PIN_DC));
 * idfxx::epaper::uc8179 display(io, {.reset_gpio = PIN_RST, .busy_gpio = PIN_BUSY});
 *
 * idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, true);
 * fb.flush(display);
 * display.refresh();
 * display.sleep();
 * @endcode
 */
class uc8179 final : public panel {
public:
    /**
     * @headerfile <idfxx/epaper/uc8179>
     * @brief Configuration structure for UC8179 panels.
     */
    struct config {
        /// Panel width in pixels (sources). Must be a multiple of 8; the
        /// controller drives up to 800. Defaults match the 7.5" 800x480
        /// GDEY075T7 glass.
        size_t width = 800;
        /// Panel height in pixels (gates). The controller drives up to 600.
        size_t height = 480;
        /// GPIO wired to the panel's reset line, or `gpio::nc()` if not
        /// wired. Required for @ref wake, and strongly recommended in
        /// general: the driver hardware-resets the controller when switching
        /// between refresh waveforms so each mode starts from a known
        /// register state.
        gpio reset_gpio = gpio::nc();
        /// GPIO wired to the panel's BUSY output (required). The UC8179
        /// drives BUSY low while an operation is in progress.
        gpio busy_gpio = gpio::nc();
        /// Maximum time to wait for the BUSY line to release. Full refreshes
        /// of large panels take several seconds on some glasses and
        /// temperatures.
        std::chrono::milliseconds busy_timeout{30'000};
    };

    /**
     * @brief Returns a panel I/O configuration for communicating with a UC8179 over SPI.
     *
     * Fills in the SPI framing the UC8179 controller requires (mode 0,
     * 8-bit commands and parameters, a dedicated D/C line), so only the
     * wiring-specific values are parameters.
     *
     * @param cs   GPIO wired to the panel's chip-select line.
     * @param dc   GPIO wired to the panel's data/command line.
     * @param pclk SPI clock frequency. The UC8179 supports up to 10 MHz
     *             for writes.
     *
     * @return A panel_io::spi_config ready to construct a panel_io.
     *
     * @code
     * idfxx::panel_io io(bus, idfxx::epaper::uc8179::spi_io_config(PIN_CS, PIN_DC));
     * idfxx::epaper::uc8179 display(io, {.busy_gpio = PIN_BUSY});
     * @endcode
     */
    [[nodiscard]] static panel_io::spi_config
    spi_io_config(gpio cs, gpio dc, freq::hertz pclk = freq::hertz{10'000'000}) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new UC8179 panel driver.
     *
     * Configures the BUSY and reset GPIOs, hardware-resets the controller,
     * runs the full initialization sequence, and clears both image planes
     * to white, leaving the panel awake in @ref color_mode::mono and ready
     * for writes.
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
    [[nodiscard]] explicit uc8179(idfxx::panel_io& panel_io, config config);
#endif

    /**
     * @brief Creates a new UC8179 panel driver.
     *
     * Configures the BUSY and reset GPIOs, hardware-resets the controller,
     * runs the full initialization sequence, and clears both image planes
     * to white, leaving the panel awake in @ref color_mode::mono and ready
     * for writes.
     *
     * Does not take ownership of @p panel_io. It is the caller's
     * responsibility to ensure that this panel does not outlive the panel
     * I/O interface.
     *
     * @param panel_io The panel I/O interface.
     * @param config   Panel configuration; `busy_gpio` is required.
     *
     * @return The new uc8179, or an error.
     * @retval idfxx::errc::invalid_arg if `busy_gpio` is not connected, the
     *         width is zero, not a multiple of 8, or over 800, or the
     *         height is zero or over 600.
     * @retval idfxx::errc::timeout if the controller's BUSY line does not
     *         release during initialization.
     */
    [[nodiscard]] static result<uc8179> make(idfxx::panel_io& panel_io, config config);

    ~uc8179() override;

    uc8179(const uc8179&) = delete;
    uc8179& operator=(const uc8179&) = delete;
    uc8179(uc8179&& other) noexcept;
    uc8179& operator=(uc8179&& other) noexcept;

private:
    // Waveform configuration currently loaded into the controller. The
    // UC8179 selects its update waveform at initialization (OTP banks or
    // register-written LUTs), not per refresh command, so the driver
    // re-initializes when a refresh needs a different waveform.
    enum class waveform : uint8_t { full, fast, partial, gray };

    // A rectangular pixel region of the panel, for partial-window
    // programming.
    struct region {
        size_t x;
        size_t y;
        size_t width;
        size_t height;
    };

    // Tag distinguishing the non-initializing constructor make() uses from
    // the public throwing constructor with the same argument list.
    struct raw_tag {};

    explicit uc8179(raw_tag, idfxx::panel_io& panel_io, config config)
        : panel(config.width, config.height, config.busy_gpio, gpio::level::low, config.busy_timeout, config.reset_gpio)
        , _io(&panel_io) {}

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

    // Command sequencing helpers (see src/uc8179.cpp).
    [[nodiscard]] result<void> _cmd(uint8_t cmd);
    [[nodiscard]] result<void> _cmd(uint8_t cmd, std::initializer_list<uint8_t> params);
    [[nodiscard]] result<void> _cmd(uint8_t cmd, std::span<const uint8_t> params);
    [[nodiscard]] result<void> _stream(int cmd, std::span<const uint8_t> data);
    [[nodiscard]] result<void> _power_on();
    [[nodiscard]] result<void> _set_resolution();
    [[nodiscard]] result<void> _load_luts(
        std::span<const uint8_t> vcom,
        std::span<const uint8_t> ww,
        std::span<const uint8_t> kw,
        std::span<const uint8_t> wk,
        std::span<const uint8_t> kk
    );
    [[nodiscard]] result<void> _init_full();
    [[nodiscard]] result<void> _init_fast();
    [[nodiscard]] result<void> _init_partial();
    [[nodiscard]] result<void> _init_gray();
    [[nodiscard]] result<void> _ensure_waveform(waveform target);
    [[nodiscard]] result<void> _window_begin(region r);
    [[nodiscard]] result<void> _window_end();
    [[nodiscard]] result<void> _clear_ram(bool include_old_plane);
    [[nodiscard]] result<void> _update();
    [[nodiscard]] result<void> _enter_deep_sleep();

    idfxx::panel_io* _io = nullptr; // nullptr after move
    waveform _waveform = waveform::full;
};

} // namespace idfxx::epaper
