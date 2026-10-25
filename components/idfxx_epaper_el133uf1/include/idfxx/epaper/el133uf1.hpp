// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/el133uf1>
 * @file el133uf1.hpp
 * @brief EL133UF1 (13.3" Spectra 6) ePaper panel driver.
 * @ingroup idfxx_epaper
 */

#include "sdkconfig.h"

#include <idfxx/epaper/panel>
#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/memory>
#include <idfxx/required>
#include <idfxx/spi/master>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <quanta/frequency>
#include <span>
#include <utility>
#include <vector>

#ifndef CONFIG_SPIRAM
#error "idfxx_epaper_el133uf1 requires PSRAM: enable CONFIG_SPIRAM (menuconfig -> Component config -> ESP PSRAM)"
#endif

/**
 * @headerfile <idfxx/epaper/el133uf1>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/el133uf1>
 * @brief EL133UF1 13.3" six-color (E Ink Spectra 6) ePaper display driver.
 *
 * Driver for the E Ink EL133UF1 13.3" 1200x1600 Spectra 6 glass, as used in
 * modules such as Seeed Studio's T133A01 (in the reTerminal E1004).
 * Implements the @ref idfxx::epaper::panel interface in
 * @ref color_mode::color6 (the native format, six colors at 4 bits per
 * pixel) and @ref color_mode::mono (1-bpp framebuffers rendered as black
 * and white, so existing text and banded-rendering code works unchanged).
 * The glass supports full refreshes only: `fast` and `partial` refresh
 * requests always report `errc::not_supported` (even before a baseline
 * exists), as does @ref color_mode::gray4. A full refresh takes on the
 * order of 25-30 seconds.
 *
 * The panel is driven by two cascaded controller ICs, each owning one
 * 600-column half of every row: the master (columns 0-599) and the slave
 * (columns 600-1199). They share the SPI clock and data lines, the
 * data/command line, and a single BUSY output (active low), and each has
 * its own chip-select line. The driver attaches its own device to an
 * `idfxx::spi::master_bus` and drives both chip selects and the
 * data/command line itself, so a command addressed to both controllers
 * reaches them in a single transfer with both selected. It also owns the
 * BUSY input and reset output lines.
 *
 * The panel's data output is optional. When it is wired to the bus's MISO
 * line, setting @ref config::check_presence makes the driver confirm that
 * both controllers answer after every hardware reset, so a disconnected or
 * unpowered panel is reported instead of appearing idle.
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
 * Each command and each half-frame upload holds the SPI bus exclusively,
 * from chip-select assertion to release, so transactions to other devices
 * on the same bus wait until it finishes. The bus is free while the driver
 * waits on BUSY.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior. The destructor powers off an awake controller pair and puts
 * them into deep sleep.
 *
 * @code
 * idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, {
 *     .mosi = PIN_MOSI,
 *     .sclk = PIN_SCLK,
 * });
 * idfxx::epaper::el133uf1 display(bus, {
 *     .master_cs_gpio = PIN_CS,
 *     .slave_cs_gpio = PIN_CS1,
 *     .dc_gpio = PIN_DC,
 *     .reset_gpio = PIN_RST,
 *     .busy_gpio = PIN_BUSY,
 * });
 *
 * idfxx::epaper::color6_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, idfxx::epaper::color6::red);
 * display.write(fb);
 * display.refresh();
 * display.sleep();
 * @endcode
 */
class el133uf1 final : public panel {
public:
    /// Panel width in pixels (the portrait-native orientation).
    static constexpr size_t native_width = 1200;
    /// Panel height in pixels (the portrait-native orientation).
    static constexpr size_t native_height = 1600;

    /**
     * @headerfile <idfxx/epaper/el133uf1>
     * @brief Configuration structure for EL133UF1 panels.
     */
    struct config {
        /// GPIO wired to the master controller's chip-select line (CSB_M),
        /// which selects the controller driving columns 0-599.
        required<gpio> master_cs_gpio;
        /// GPIO wired to the slave controller's chip-select line (CSB_S),
        /// which selects the controller driving columns 600-1199. Must
        /// differ from `master_cs_gpio`.
        required<gpio> slave_cs_gpio;
        /// GPIO wired to the panel's data/command line, shared by both
        /// controllers.
        required<gpio> dc_gpio;
        /// GPIO wired to the panel's reset line, or `gpio::nc()` if not
        /// wired. Required for @ref wake; strongly recommended so the
        /// controllers start from a known register state.
        gpio reset_gpio = gpio::nc();
        /// GPIO wired to the panel's BUSY output. The controllers drive
        /// BUSY low while an operation is in progress.
        required<gpio> busy_gpio;
        /// Whether to confirm that both controllers are present. When set,
        /// the driver reads each controller's ID register after every
        /// hardware reset (during construction and @ref wake) and fails
        /// with `errc::not_found` if either controller does not answer.
        /// Requires the panel's data output to be wired to the bus's MISO
        /// line.
        bool check_presence = false;
        /// SPI clock frequency. The controllers accept up to 10 MHz for
        /// writes.
        quanta::frequency::hertz pclk{10'000'000};
        /// Maximum time to wait for the BUSY line to release. A full
        /// six-color refresh takes 25-30 seconds, longer when cold.
        std::chrono::milliseconds busy_timeout{60'000};
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new EL133UF1 panel driver on the given SPI bus.
     *
     * Adds a device for the panel to @p bus, configures the chip-select,
     * data/command, BUSY and reset GPIOs, hardware-resets the controllers
     * (confirming that both answer, if `config.check_presence` is set),
     * runs the full initialization sequence, and allocates the PSRAM shadow
     * frame (blank white), leaving the panel awake in
     * @ref color_mode::color6 and ready for writes.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus    SPI master bus the panel is wired to. MOSI and SCLK are
     *               always used; MISO is used only when
     *               `config.check_presence` is set.
     * @param config Panel configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg`
     *         if the two chip-select GPIOs are the same or a required GPIO
     *         is unconnected, `errc::timeout` if the BUSY line does not
     *         release during initialization, and `errc::not_found` if
     *         `config.check_presence` is set and a controller does not
     *         answer.
     *
     * @code
     * idfxx::epaper::el133uf1 display(bus, {
     *     .master_cs_gpio = PIN_CS,
     *     .slave_cs_gpio = PIN_CS1,
     *     .dc_gpio = PIN_DC,
     *     .reset_gpio = PIN_RST,
     *     .busy_gpio = PIN_BUSY,
     * });
     * @endcode
     */
    [[nodiscard]] explicit el133uf1(spi::master_bus& bus, config config);
#endif

    /**
     * @brief Creates a new EL133UF1 panel driver on the given SPI bus.
     *
     * Adds a device for the panel to @p bus, configures the chip-select,
     * data/command, BUSY and reset GPIOs, hardware-resets the controllers
     * (confirming that both answer, if `config.check_presence` is set),
     * runs the full initialization sequence, and allocates the PSRAM shadow
     * frame (blank white), leaving the panel awake in
     * @ref color_mode::color6 and ready for writes.
     *
     * Does not take ownership of @p bus. The caller must ensure the bus
     * outlives this driver.
     *
     * @param bus    SPI master bus the panel is wired to. MOSI and SCLK are
     *               always used; MISO is used only when
     *               `config.check_presence` is set.
     * @param config Panel configuration.
     *
     * @return The new el133uf1, or an error. Errors adding the panel's
     *         device to @p bus are passed through.
     * @retval idfxx::errc::invalid_arg if `master_cs_gpio` and
     *         `slave_cs_gpio` are the same GPIO, or one of `master_cs_gpio`,
     *         `slave_cs_gpio`, `dc_gpio` and `busy_gpio` is not connected.
     * @retval idfxx::errc::timeout if the controllers' BUSY line does not
     *         release during initialization.
     * @retval idfxx::errc::not_found if `config.check_presence` is set and
     *         either controller does not answer.
     *
     * @code
     * auto display = idfxx::epaper::el133uf1::make(bus, {
     *     .master_cs_gpio = PIN_CS,
     *     .slave_cs_gpio = PIN_CS1,
     *     .dc_gpio = PIN_DC,
     *     .reset_gpio = PIN_RST,
     *     .busy_gpio = PIN_BUSY,
     * });
     * if (!display) {
     *     return idfxx::error(display.error());
     * }
     * @endcode
     */
    [[nodiscard]] static result<el133uf1> make(spi::master_bus& bus, config config);

    ~el133uf1() override;

    el133uf1(const el133uf1&) = delete;
    el133uf1& operator=(const el133uf1&) = delete;
    el133uf1(el133uf1&& other) noexcept;
    el133uf1& operator=(el133uf1&& other) noexcept;

private:
    // Which controller(s) a command is addressed to: `both` selects the two
    // controllers together for a single transfer.
    enum class target : uint8_t { master, slave, both };

    // Non-initializing constructor make() uses: takes ownership of the
    // panel's SPI device.
    explicit el133uf1(spi::master_device device, const config& config)
        : panel(native_width, native_height, config.busy_gpio, gpio::level::low, config.busy_timeout, config.reset_gpio)
        , _device(std::move(device))
        , _master_cs(config.master_cs_gpio)
        , _slave_cs(config.slave_cs_gpio)
        , _dc(config.dc_gpio)
        , _check_presence(config.check_presence) {}

    // epaper::panel customization hooks.
    [[nodiscard]] result<void>
    do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override;
    [[nodiscard]] result<void>
    do_write(const color6_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) override;
    [[nodiscard]] result<void> do_clear() override;
    [[nodiscard]] result<void> do_refresh(refresh_mode mode) override;
    [[nodiscard]] bool do_supports(enum color_mode mode) const noexcept override;
    [[nodiscard]] bool do_supports(refresh_mode mode) const noexcept override;
    [[nodiscard]] result<void> do_set_color_mode(enum color_mode mode) override;
    [[nodiscard]] result<void> do_sleep() override;
    [[nodiscard]] result<void> do_wake() override;
    [[nodiscard]] result<void> do_wait(std::optional<std::chrono::milliseconds> timeout) override;

    // Command sequencing helpers (see src/el133uf1.cpp).
    [[nodiscard]] result<void> _cmd(target to, uint8_t cmd);
    [[nodiscard]] result<void> _cmd(target to, uint8_t cmd, std::initializer_list<uint8_t> params);
    [[nodiscard]] result<void> _stream(target to, uint8_t cmd, std::span<const uint8_t> plane);
    [[nodiscard]] result<void> _send(std::span<const uint8_t> bytes);
    [[nodiscard]] result<void> _confirm_presence();
    [[nodiscard]] result<void> _init();
    [[nodiscard]] result<void> _upload();
    [[nodiscard]] result<void> _settle(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    [[nodiscard]] result<void> _enter_deep_sleep();

    // Shadow-frame addressing: each controller's plane is native_height
    // rows of native_width / 4 bytes (600 pixels at two per byte).
    [[nodiscard]] std::span<const uint8_t> _plane(size_t index) const noexcept;
    [[nodiscard]] uint8_t* _shadow_byte(size_t x, size_t y) noexcept;

    // True once moved from: moving the device clears its handle.
    [[nodiscard]] bool _moved_from() const noexcept { return _device.idf_handle() == nullptr; }

    // The panel's device on the SPI bus. It has no chip select of its own:
    // the driver drives _master_cs, _slave_cs and _dc around each transfer.
    spi::master_device _device;
    gpio _master_cs;
    gpio _slave_cs;
    gpio _dc;
    // Whether to read the controllers' ID registers after each reset.
    bool _check_presence;

    // Internal DMA-capable staging buffer for outgoing bytes (at most 4 KB,
    // capped at the bus's maximum transaction length). Shadow-plane chunks
    // are copied here out of PSRAM before each transmit.
    std::vector<uint8_t, dma_allocator<uint8_t>> _chunk;

    /// Work do_refresh leaves running so it can return while the glass
    /// updates; _settle (reached through do_wait) completes it before the
    /// next controller command.
    enum class pending : uint8_t {
        none,      ///< Controllers idle; nothing deferred.
        power_off, ///< The drive rails are up (an update may be driving); wait for BUSY, then power them off.
    };
    pending _pending = pending::none;

    // Host-side copy of the frame in the controllers' upload format: two
    // contiguous per-controller planes (plane 0 = master, plane 1 = slave),
    // each native_height rows of 300 bytes. Writes land here; refresh
    // streams each plane to its controller.
    std::vector<uint8_t, spiram_allocator<uint8_t>> _shadow;
};

} // namespace idfxx::epaper
