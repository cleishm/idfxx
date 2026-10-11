// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/t133a01>
 * @file t133a01.hpp
 * @brief T133A01 (13.3" Spectra 6) ePaper panel driver.
 * @ingroup idfxx_epaper
 */

#include "sdkconfig.h"

#include <idfxx/epaper/panel>
#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/memory>
#include <idfxx/panel_io>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <quanta/frequency>
#include <span>
#include <vector>

#ifndef CONFIG_SPIRAM
#error "idfxx_epaper_t133a01 requires PSRAM: enable CONFIG_SPIRAM (menuconfig -> Component config -> ESP PSRAM)"
#endif

/**
 * @headerfile <idfxx/epaper/t133a01>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/t133a01>
 * @brief T133A01 13.3" six-color (E Ink Spectra 6) ePaper display driver.
 *
 * Driver for the E Ink T133A01 module — the 13.3" 1200x1600 Spectra 6 glass
 * on the Seeed Studio reTerminal E1004 and XIAO ePaper DIY Kit EE02.
 * Implements the @ref idfxx::epaper::panel interface in
 * @ref color_mode::color6 (the native format, six colors at 4 bits per
 * pixel) and @ref color_mode::mono (1-bpp framebuffers rendered as black
 * and white, so existing text and banded-rendering code works unchanged).
 * The glass supports full refreshes only: `fast` and `partial` refresh
 * modes, and @ref color_mode::gray4, report `errc::not_supported`. A full
 * refresh takes on the order of 30-40 seconds.
 *
 * The panel is driven by two controller ICs, each owning one 600-column
 * half of every row, selected by separate chip-select lines. The driver
 * therefore takes two `idfxx::panel_io` objects on the same SPI bus: the
 * master (the controller for columns 0-599) and the slave (columns
 * 600-1199), both sharing the data/command line and configured with
 * @ref spi_io_config. The driver owns the BUSY input (active low) and
 * reset output lines.
 *
 * Because the controllers have no addressable window, every refresh
 * uploads the whole frame. The driver keeps a 960 KB shadow of the frame in
 * PSRAM: writes (at any row, and any even column in color6 or any multiple
 * of 8 in mono) update the shadow only, and @ref refresh streams it to the
 * two controllers before driving the update — so band-by-band rendering
 * through a small framebuffer works without a frame-sized application
 * buffer. **PSRAM is required:** this header fails to compile unless
 * `CONFIG_SPIRAM` is enabled.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior. The destructor powers off an awake controller pair and puts
 * them into deep sleep.
 *
 * @code
 * idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);
 * idfxx::panel_io master(bus, idfxx::epaper::t133a01::spi_io_config(PIN_CS, PIN_DC));
 * idfxx::panel_io slave(bus, idfxx::epaper::t133a01::spi_io_config(PIN_CS1, PIN_DC));
 * idfxx::epaper::t133a01 display(master, slave, {.reset_gpio = PIN_RST, .busy_gpio = PIN_BUSY});
 *
 * idfxx::epaper::color6_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, idfxx::epaper::color6::red);
 * display.write(fb);
 * display.refresh();
 * display.sleep();
 * @endcode
 */
class t133a01 final : public panel {
public:
    /// Panel width in pixels (the portrait-native orientation).
    static constexpr size_t native_width = 1200;
    /// Panel height in pixels (the portrait-native orientation).
    static constexpr size_t native_height = 1600;

    /**
     * @headerfile <idfxx/epaper/t133a01>
     * @brief Configuration structure for T133A01 panels.
     */
    struct config {
        /// GPIO wired to the panel's reset line, or `gpio::nc()` if not
        /// wired. Required for @ref wake; strongly recommended so the
        /// controllers start from a known register state.
        gpio reset_gpio = gpio::nc();
        /// GPIO wired to the panel's BUSY output (required). The
        /// controllers drive BUSY low while an operation is in progress.
        gpio busy_gpio = gpio::nc();
        /// Maximum time to wait for the BUSY line to release. A full
        /// six-color refresh takes 30-40 seconds, longer when cold.
        std::chrono::milliseconds busy_timeout{60'000};
    };

    /**
     * @brief Returns a panel I/O configuration for communicating with one
     *        T133A01 controller over SPI.
     *
     * Fills in the SPI framing the controllers require (mode 0, 8-bit
     * commands and parameters, a dedicated D/C line), so only the
     * wiring-specific values are parameters. Call it twice — once per
     * chip-select line — to build the master and slave `panel_io`s.
     *
     * @param cs   GPIO wired to this controller's chip-select line.
     * @param dc   GPIO wired to the panel's data/command line (shared by
     *             both controllers).
     * @param pclk SPI clock frequency. The controllers accept up to 10 MHz
     *             for writes.
     *
     * @return A panel_io::spi_config ready to construct a panel_io.
     *
     * @code
     * idfxx::panel_io master(bus, idfxx::epaper::t133a01::spi_io_config(PIN_CS, PIN_DC));
     * idfxx::panel_io slave(bus, idfxx::epaper::t133a01::spi_io_config(PIN_CS1, PIN_DC));
     * idfxx::epaper::t133a01 display(master, slave, {.reset_gpio = PIN_RST, .busy_gpio = PIN_BUSY});
     * @endcode
     */
    [[nodiscard]] static panel_io::spi_config
    spi_io_config(gpio cs, gpio dc, quanta::frequency::hertz pclk = quanta::frequency::hertz{10'000'000}) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new T133A01 panel driver.
     *
     * Configures the BUSY and reset GPIOs, hardware-resets the controllers,
     * runs the full initialization sequence, and allocates the PSRAM shadow
     * frame (blank white), leaving the panel awake in
     * @ref color_mode::color6 and ready for writes.
     *
     * Does not take ownership of the panel I/O objects. It is the caller's
     * responsibility to ensure that this panel does not outlive them.
     *
     * @param master_io Panel I/O for the master controller (chip select of
     *                  the controller driving columns 0-599).
     * @param slave_io  Panel I/O for the slave controller (chip select of
     *                  the controller driving columns 600-1199).
     * @param config    Panel configuration; `busy_gpio` is required.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg`
     *         if the two I/O objects are the same or `busy_gpio` is
     *         unconnected, and `errc::timeout` if the BUSY line does not
     *         release during initialization.
     */
    [[nodiscard]] t133a01(idfxx::panel_io& master_io, idfxx::panel_io& slave_io, config config);
#endif

    /**
     * @brief Creates a new T133A01 panel driver.
     *
     * Configures the BUSY and reset GPIOs, hardware-resets the controllers,
     * runs the full initialization sequence, and allocates the PSRAM shadow
     * frame (blank white), leaving the panel awake in
     * @ref color_mode::color6 and ready for writes.
     *
     * Does not take ownership of the panel I/O objects. It is the caller's
     * responsibility to ensure that this panel does not outlive them.
     *
     * @param master_io Panel I/O for the master controller (chip select of
     *                  the controller driving columns 0-599).
     * @param slave_io  Panel I/O for the slave controller (chip select of
     *                  the controller driving columns 600-1199).
     * @param config    Panel configuration; `busy_gpio` is required.
     *
     * @return The new t133a01, or an error.
     * @retval idfxx::errc::invalid_arg if @p master_io and @p slave_io are
     *         the same object, or `busy_gpio` is not connected.
     * @retval idfxx::errc::timeout if the controllers' BUSY line does not
     *         release during initialization.
     */
    [[nodiscard]] static result<t133a01> make(idfxx::panel_io& master_io, idfxx::panel_io& slave_io, config config);

    ~t133a01() override;

    t133a01(const t133a01&) = delete;
    t133a01& operator=(const t133a01&) = delete;
    t133a01(t133a01&& other) noexcept;
    t133a01& operator=(t133a01&& other) noexcept;

private:
    // Which controller(s) a command is addressed to. Broadcasts go to the
    // master first, then the slave.
    enum class target : uint8_t { master, slave, both };

    // Tag distinguishing the non-initializing constructor make() uses from
    // the public throwing constructor with the same argument list.
    struct raw_tag {};

    explicit t133a01(raw_tag, idfxx::panel_io& master_io, idfxx::panel_io& slave_io, config config)
        : panel(native_width, native_height, config.busy_gpio, gpio::level::low, config.busy_timeout, config.reset_gpio)
        , _master(&master_io)
        , _slave(&slave_io) {}

    // epaper::panel customization hooks.
    [[nodiscard]] result<void>
    do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override;
    [[nodiscard]] result<void>
    do_write(const color6_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override;
    [[nodiscard]] result<void> do_clear() override;
    [[nodiscard]] result<void> do_refresh(refresh_mode mode) override;
    [[nodiscard]] bool do_supports(enum color_mode mode) const noexcept override;
    [[nodiscard]] result<void> do_set_color_mode(enum color_mode mode) override;
    [[nodiscard]] result<void> do_sleep() override;
    [[nodiscard]] result<void> do_wake() override;
    [[nodiscard]] result<void> do_wait(std::optional<std::chrono::milliseconds> timeout) override;

    // Command sequencing helpers (see src/t133a01.cpp).
    [[nodiscard]] result<void> _cmd(target to, uint8_t cmd);
    [[nodiscard]] result<void> _cmd(target to, uint8_t cmd, std::initializer_list<uint8_t> params);
    [[nodiscard]] result<void> _stream(idfxx::panel_io& io, uint8_t cmd, std::span<const uint8_t> data);
    [[nodiscard]] result<void> _reset();
    [[nodiscard]] result<void> _init();
    [[nodiscard]] result<void> _upload();
    [[nodiscard]] result<void> _settle(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    [[nodiscard]] result<void> _enter_deep_sleep();

    // Shadow-frame addressing: each controller's plane is native_height
    // rows of native_width / 4 bytes (600 pixels at two per byte).
    [[nodiscard]] std::span<uint8_t> _plane(size_t index) noexcept;
    [[nodiscard]] std::span<const uint8_t> _plane(size_t index) const noexcept;
    [[nodiscard]] uint8_t* _shadow_byte(size_t x, size_t y) noexcept;

    idfxx::panel_io* _master = nullptr; // nullptr after move
    idfxx::panel_io* _slave = nullptr;

    /// Work do_refresh leaves running so it can return while the glass
    /// updates; _settle (reached through do_wait) completes it before the
    /// next controller command.
    enum class pending : uint8_t {
        none,      ///< Controllers idle; nothing deferred.
        power_off, ///< An update is driving; wait for BUSY, then power off the rails.
    };
    pending _pending = pending::none;

    // Host-side copy of the frame in the controllers' upload format: two
    // contiguous per-controller planes (plane 0 = master, plane 1 = slave),
    // each native_height rows of 300 bytes. Writes land here; refresh
    // streams each plane to its controller in one transfer.
    std::vector<uint8_t, spiram_allocator<uint8_t>> _shadow;
};

} // namespace idfxx::epaper
