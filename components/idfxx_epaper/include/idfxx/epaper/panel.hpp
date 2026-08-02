// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/panel>
 * @file panel.hpp
 * @brief Abstract ePaper panel interface.
 *
 * @defgroup idfxx_epaper ePaper Component
 * @brief Abstract ePaper panel interface, framebuffers, and shared types.
 *
 * Provides a controller-agnostic abstract base class (`idfxx::epaper::panel`)
 * that concrete drivers — `idfxx::epaper::ssd1680`, `idfxx::epaper::uc8179`,
 * and future siblings — implement, plus the framebuffers
 * (`idfxx::epaper::mono_framebuffer`, `idfxx::epaper::gray4_framebuffer`)
 * that hold pixel data in the row-major layouts ePaper controllers consume.
 *
 * The interface models the ePaper update cycle rather than an
 * immediate-mode display: pixel data is first uploaded to the controller's
 * RAM with @ref idfxx::epaper::panel::write (nothing changes on the glass),
 * then made visible with @ref idfxx::epaper::panel::refresh, which drives
 * the physical ink update and blocks until the controller's BUSY line
 * releases. Panels support full refreshes, faster reduced-quality full
 * refreshes, flicker-free partial refreshes, and 4-level grayscale,
 * subject to each driver's capabilities.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for the
 * BUSY and reset lines.
 * @{
 */

#include <idfxx/error>
#include <idfxx/gpio>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * @headerfile <idfxx/epaper/panel>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

class mono_framebuffer;
class gray4_framebuffer;

/**
 * @headerfile <idfxx/epaper/panel>
 * @brief Refresh style for @ref panel::refresh.
 *
 * ePaper controllers offer several waveform families trading update speed
 * and flicker against image quality and ghosting.
 */
enum class refresh_mode : uint8_t {
    /// Full update with the controller's highest-quality waveform: the
    /// panel flashes through inverse images before settling. Slowest, but
    /// clears ghosting completely.
    full,
    /// Full-screen update with a shortened waveform: much faster than
    /// @ref refresh_mode::full and still updates every pixel, at some cost
    /// in contrast and ghosting. Not supported by every driver.
    fast,
    /// Differential update: only pixels that changed since the previous
    /// refresh flip, without the full-refresh flash. Fastest and
    /// flicker-free, but accumulates ghosting — issue a
    /// @ref refresh_mode::full refresh periodically to clean the panel.
    partial,
};

/**
 * @headerfile <idfxx/epaper/panel>
 * @brief Pixel format the panel is operating in.
 *
 * Selected with @ref panel::set_color_mode; determines which framebuffer
 * type @ref panel::write accepts.
 */
enum class color_mode : uint8_t {
    mono,  ///< 1 bit per pixel black-and-white (@ref mono_framebuffer).
    gray4, ///< 2 bits per pixel, 4 gray levels (@ref gray4_framebuffer).
};

/**
 * @headerfile <idfxx/epaper/panel>
 * @brief Abstract base class for ePaper display panels.
 *
 * The public interface is non-virtual; concrete drivers (e.g.
 * `idfxx::epaper::ssd1680`) customize behaviour by overriding the protected
 * `do_*` hooks, mirroring the standard library's non-virtual-interface
 * pattern (cf. `std::pmr::memory_resource`).
 *
 * The interface follows the ePaper update cycle:
 *
 * 1. @ref write uploads framebuffer pixels to the controller's RAM at a
 *    pixel offset. Nothing changes on the glass yet, and writes may be
 *    repeated to compose a frame from multiple buffers. A write does not
 *    retain a reference to the framebuffer: it may be redrawn immediately
 *    after the call returns.
 * 2. @ref refresh drives the physical ink update from the controller's RAM
 *    and blocks until the panel's BUSY line reports completion.
 * 3. @ref sleep puts the controller into deep sleep between updates —
 *    essential for ePaper longevity and power; @ref wake restores it.
 *
 * @code
 * idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
 * idfxx::gfx::canvas canvas(fb);
 * canvas.draw_text(idfxx::font::spleen_8x16, 4, 4, "hello", true);
 * fb.flush(display);                              // upload to controller RAM
 * display.refresh();                              // make it visible
 * display.sleep();                                // deep sleep until the next update
 * @endcode
 */
class panel {
public:
    virtual ~panel() = default;

    panel(const panel&) = delete;
    panel& operator=(const panel&) = delete;

    // =========================================================================
    // Accessors
    // =========================================================================

    /** @brief Returns the panel width in pixels. */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /** @brief Returns the panel height in pixels. */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Returns the panel's current pixel format.
     *
     * Reflects the most recent successful @ref try_set_color_mode /
     * @ref set_color_mode, or @ref color_mode::mono before any call.
     *
     * @return The current color mode.
     */
    [[nodiscard]] enum color_mode color_mode() const noexcept { return _color_mode; }

    /**
     * @brief Returns whether the panel is in deep sleep.
     *
     * True after a successful @ref sleep and until the next successful
     * @ref wake. While asleep, writes and refreshes report
     * `errc::invalid_state`.
     *
     * @return true if the panel is in deep sleep.
     */
    [[nodiscard]] bool asleep() const noexcept { return _asleep; }

    // =========================================================================
    // RAM upload
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Uploads a monochrome framebuffer to the controller's RAM.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y). The upload is
     * invisible until the next @ref refresh. The panel must be operating in
     * @ref color_mode::mono.
     *
     * @param fb The framebuffer to upload.
     * @param x  Destination column; must be a multiple of 8 (controller RAM
     *           is byte-packed along the row).
     * @param y  Destination row.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg`
     *         for an unaligned @p x or an out-of-bounds placement, and
     *         `errc::invalid_state` if the panel is asleep or in
     *         @ref color_mode::gray4.
     */
    void write(const mono_framebuffer& fb, size_t x = 0, size_t y = 0) { unwrap(try_write(fb, x, y)); }

    /**
     * @brief Uploads a grayscale framebuffer to the controller's RAM.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y). The upload is
     * invisible until the next @ref refresh. The panel must be operating in
     * @ref color_mode::gray4.
     *
     * @param fb The framebuffer to upload.
     * @param x  Destination column; must be a multiple of 8 (controller RAM
     *           is byte-packed along the row).
     * @param y  Destination row.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg`
     *         for an unaligned @p x or an out-of-bounds placement, and
     *         `errc::invalid_state` if the panel is asleep or in
     *         @ref color_mode::mono.
     */
    void write(const gray4_framebuffer& fb, size_t x = 0, size_t y = 0) { unwrap(try_write(fb, x, y)); }

    /**
     * @brief Uploads a horizontal band of a monochrome framebuffer.
     *
     * Uploads rows `[row_start, row_end)` of @p fb, with the framebuffer's
     * origin at pixel (@p x, @p y) — row `r` of the framebuffer lands at
     * panel row `y + r`, so a band flushed from a full-frame framebuffer
     * lands exactly where the full upload would place it.
     *
     * @param fb        The framebuffer to upload from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin; must be
     *                  a multiple of 8.
     * @param y         Destination row of the framebuffer origin.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg`
     *         for an invalid row range, an unaligned @p x, or an
     *         out-of-bounds placement, and `errc::invalid_state` if the
     *         panel is asleep or in @ref color_mode::gray4.
     */
    void write_rows(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0) {
        unwrap(try_write_rows(fb, row_start, row_end, x, y));
    }

    /**
     * @brief Uploads a horizontal band of a grayscale framebuffer.
     *
     * Uploads rows `[row_start, row_end)` of @p fb, with the framebuffer's
     * origin at pixel (@p x, @p y) — row `r` of the framebuffer lands at
     * panel row `y + r`.
     *
     * @param fb        The framebuffer to upload from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin; must be
     *                  a multiple of 8.
     * @param y         Destination row of the framebuffer origin.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_arg`
     *         for an invalid row range, an unaligned @p x, or an
     *         out-of-bounds placement, and `errc::invalid_state` if the
     *         panel is asleep or in @ref color_mode::mono.
     */
    void write_rows(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0) {
        unwrap(try_write_rows(fb, row_start, row_end, x, y));
    }

    /**
     * @brief Clears the controller's RAM to white, without a framebuffer.
     *
     * Fills the entire image RAM with white (blank paper) in the current
     * color mode. Like a write, the cleared frame is invisible until the
     * next @ref refresh — which is promoted to @ref refresh_mode::full,
     * since the cleared RAM no longer matches what a partial refresh would
     * diff against.
     *
     * Useful for blanking the display, or for erasing previous content
     * before switching to @ref color_mode::gray4, whose waveform erases
     * less aggressively than a monochrome full refresh.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_state`
     *         if the panel is asleep.
     */
    void clear() { unwrap(try_clear()); }
#endif

    /**
     * @brief Uploads a monochrome framebuffer to the controller's RAM.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y). The upload is
     * invisible until the next @ref try_refresh. The panel must be operating
     * in @ref color_mode::mono.
     *
     * @param fb The framebuffer to upload.
     * @param x  Destination column; must be a multiple of 8 (controller RAM
     *           is byte-packed along the row).
     * @param y  Destination row.
     * @return Success, or an error.
     * @retval invalid_arg @p x is not a multiple of 8, or the framebuffer
     *         does not fit within the panel at (@p x, @p y).
     * @retval invalid_state The panel is asleep, or is operating in
     *         @ref color_mode::gray4.
     */
    [[nodiscard]] result<void> try_write(const mono_framebuffer& fb, size_t x = 0, size_t y = 0);

    /**
     * @brief Uploads a grayscale framebuffer to the controller's RAM.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y). The upload is
     * invisible until the next @ref try_refresh. The panel must be operating
     * in @ref color_mode::gray4.
     *
     * @param fb The framebuffer to upload.
     * @param x  Destination column; must be a multiple of 8 (controller RAM
     *           is byte-packed along the row).
     * @param y  Destination row.
     * @return Success, or an error.
     * @retval invalid_arg @p x is not a multiple of 8, or the framebuffer
     *         does not fit within the panel at (@p x, @p y).
     * @retval invalid_state The panel is asleep, or is operating in
     *         @ref color_mode::mono.
     */
    [[nodiscard]] result<void> try_write(const gray4_framebuffer& fb, size_t x = 0, size_t y = 0);

    /**
     * @brief Uploads a horizontal band of a monochrome framebuffer.
     *
     * Uploads rows `[row_start, row_end)` of @p fb, with the framebuffer's
     * origin at pixel (@p x, @p y) — row `r` of the framebuffer lands at
     * panel row `y + r`, so a band flushed from a full-frame framebuffer
     * lands exactly where the full upload would place it.
     *
     * @param fb        The framebuffer to upload from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin; must be
     *                  a multiple of 8.
     * @param y         Destination row of the framebuffer origin.
     * @return Success, or an error.
     * @retval invalid_arg The row range is invalid, @p x is not a multiple
     *         of 8, or the band does not fit within the panel.
     * @retval invalid_state The panel is asleep, or is operating in
     *         @ref color_mode::gray4.
     */
    [[nodiscard]] result<void>
    try_write_rows(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0);

    /**
     * @brief Uploads a horizontal band of a grayscale framebuffer.
     *
     * Uploads rows `[row_start, row_end)` of @p fb, with the framebuffer's
     * origin at pixel (@p x, @p y) — row `r` of the framebuffer lands at
     * panel row `y + r`.
     *
     * @param fb        The framebuffer to upload from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin; must be
     *                  a multiple of 8.
     * @param y         Destination row of the framebuffer origin.
     * @return Success, or an error.
     * @retval invalid_arg The row range is invalid, @p x is not a multiple
     *         of 8, or the band does not fit within the panel.
     * @retval invalid_state The panel is asleep, or is operating in
     *         @ref color_mode::mono.
     */
    [[nodiscard]] result<void>
    try_write_rows(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0);

    /**
     * @brief Clears the controller's RAM to white, without a framebuffer.
     *
     * Fills the entire image RAM with white (blank paper) in the current
     * color mode. Like a write, the cleared frame is invisible until the
     * next @ref try_refresh — which is promoted to @ref refresh_mode::full,
     * since the cleared RAM no longer matches what a partial refresh would
     * diff against.
     *
     * Useful for blanking the display, or for erasing previous content
     * before switching to @ref color_mode::gray4, whose waveform erases
     * less aggressively than a monochrome full refresh.
     *
     * @return Success, or an error.
     * @retval invalid_state The panel is asleep.
     */
    [[nodiscard]] result<void> try_clear() {
        if (_asleep) {
            return error(errc::invalid_state);
        }
        if (auto r = do_clear(); !r) {
            return r;
        }
        _has_baseline = false;
        return {};
    }

    // =========================================================================
    // Refresh
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Refreshes the panel from the controller's RAM.
     *
     * Drives the physical ink update, making previously written pixel data
     * visible, and blocks until the controller's BUSY line reports
     * completion. Full refreshes take on the order of seconds; partial
     * refreshes well under a second, depending on the panel.
     *
     * A @ref refresh_mode::partial refresh needs a baseline image from a
     * previous refresh to diff against; the first refresh after
     * construction, @ref wake, or a color-mode change is silently promoted
     * to @ref refresh_mode::full.
     *
     * @param mode The refresh style to use.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::timeout` if
     *         the BUSY line does not release in time, `errc::invalid_state`
     *         if the panel is asleep, and `errc::not_supported` if the
     *         driver does not support @p mode in the current color mode.
     */
    void refresh(refresh_mode mode = refresh_mode::full) { unwrap(try_refresh(mode)); }

    /**
     * @brief Waits for the controller's BUSY line to release.
     *
     * Blocks while the controller reports it is busy, up to the driver's
     * configured default timeout. @ref refresh already waits for
     * completion; this is useful before driver-specific raw operations or
     * after recovering from an error.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::timeout` if
     *         the BUSY line does not release in time.
     */
    void wait() { unwrap(try_wait()); }

    /**
     * @brief Waits for the controller's BUSY line to release, with a timeout.
     *
     * Blocks while the controller reports it is busy, up to @p timeout.
     *
     * @tparam Rep     Duration arithmetic type.
     * @tparam Period  Duration period type.
     * @param timeout Maximum time to wait.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::timeout` if
     *         the BUSY line does not release in time.
     */
    template<typename Rep, typename Period>
    void wait_for(const std::chrono::duration<Rep, Period>& timeout) {
        unwrap(try_wait_for(timeout));
    }
#endif

    /**
     * @brief Refreshes the panel from the controller's RAM.
     *
     * Drives the physical ink update, making previously written pixel data
     * visible, and blocks until the controller's BUSY line reports
     * completion. Full refreshes take on the order of seconds; partial
     * refreshes well under a second, depending on the panel.
     *
     * A @ref refresh_mode::partial refresh needs a baseline image from a
     * previous refresh to diff against; the first refresh after
     * construction, @ref try_wake, or a color-mode change is silently
     * promoted to @ref refresh_mode::full.
     *
     * @param mode The refresh style to use.
     * @return Success, or an error.
     * @retval timeout The BUSY line did not release in time.
     * @retval invalid_state The panel is asleep.
     * @retval not_supported The driver does not support @p mode in the
     *         current color mode (e.g. `fast`/`partial` in
     *         @ref color_mode::gray4 on current drivers).
     */
    [[nodiscard]] result<void> try_refresh(refresh_mode mode = refresh_mode::full) {
        if (_asleep) {
            return error(errc::invalid_state);
        }
        if (mode == refresh_mode::partial && !_has_baseline) {
            mode = refresh_mode::full;
        }
        if (auto r = do_refresh(mode); !r) {
            return r;
        }
        _has_baseline = true;
        return {};
    }

    /**
     * @brief Waits for the controller's BUSY line to release.
     *
     * Blocks while the controller reports it is busy, up to the driver's
     * configured default timeout. @ref try_refresh already waits for
     * completion; this is useful before driver-specific raw operations or
     * after recovering from an error.
     *
     * @return Success, or an error.
     * @retval timeout The BUSY line did not release in time.
     */
    [[nodiscard]] result<void> try_wait() { return do_wait(std::nullopt); }

    /**
     * @brief Waits for the controller's BUSY line to release, with a timeout.
     *
     * Blocks while the controller reports it is busy, up to @p timeout.
     *
     * @tparam Rep     Duration arithmetic type.
     * @tparam Period  Duration period type.
     * @param timeout Maximum time to wait.
     * @return Success, or an error.
     * @retval timeout The BUSY line did not release within @p timeout.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_wait_for(const std::chrono::duration<Rep, Period>& timeout) {
        return do_wait(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    // =========================================================================
    // Color mode
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Switches the panel between monochrome and grayscale operation.
     *
     * Reconfigures the controller's waveforms for the new pixel format and
     * invalidates the partial-refresh baseline: the next refresh is
     * promoted to @ref refresh_mode::full. A no-op if the panel is already
     * in the requested mode.
     *
     * @param mode The pixel format to operate in.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_state`
     *         if the panel is asleep, and `errc::not_supported` if the
     *         driver has no grayscale support.
     */
    void set_color_mode(enum color_mode mode) { unwrap(try_set_color_mode(mode)); }
#endif

    /**
     * @brief Switches the panel between monochrome and grayscale operation.
     *
     * Reconfigures the controller's waveforms for the new pixel format and
     * invalidates the partial-refresh baseline: the next refresh is
     * promoted to @ref refresh_mode::full. A no-op if the panel is already
     * in the requested mode.
     *
     * @param mode The pixel format to operate in.
     * @return Success, or an error.
     * @retval invalid_state The panel is asleep.
     * @retval not_supported The driver has no grayscale support.
     */
    [[nodiscard]] result<void> try_set_color_mode(enum color_mode mode) {
        if (_asleep) {
            return error(errc::invalid_state);
        }
        if (mode == _color_mode) {
            return {};
        }
        if (auto r = do_set_color_mode(mode); !r) {
            return r;
        }
        _color_mode = mode;
        _has_baseline = false;
        return {};
    }

    // =========================================================================
    // Power management
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Puts the panel controller into deep sleep.
     *
     * ePaper retains its image without power, so sleeping between updates
     * costs nothing visually and is strongly recommended — both for power
     * and for panel longevity (controllers left active can degrade the
     * glass). While asleep, writes and refreshes report
     * `errc::invalid_state`; call @ref wake to resume. A no-op if the panel
     * is already asleep.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure.
     */
    void sleep() { unwrap(try_sleep()); }

    /**
     * @brief Wakes the panel controller from deep sleep.
     *
     * ePaper controllers require a hardware reset to leave deep sleep, so
     * this pulses the driver's reset line and re-runs the full controller
     * initialization, restoring the configured color mode. The
     * partial-refresh baseline is lost: the next refresh is promoted to
     * @ref refresh_mode::full.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on failure, including `errc::invalid_state`
     *         if no reset line is configured.
     */
    void wake() { unwrap(try_wake()); }
#endif

    /**
     * @brief Puts the panel controller into deep sleep.
     *
     * ePaper retains its image without power, so sleeping between updates
     * costs nothing visually and is strongly recommended — both for power
     * and for panel longevity (controllers left active can degrade the
     * glass). While asleep, writes and refreshes report
     * `errc::invalid_state`; call @ref try_wake to resume. A no-op if the
     * panel is already asleep.
     *
     * @return Success, or an error.
     */
    result<void> try_sleep() {
        if (_asleep) {
            return {};
        }
        if (auto r = do_sleep(); !r) {
            return r;
        }
        _asleep = true;
        return {};
    }

    /**
     * @brief Wakes the panel controller from deep sleep.
     *
     * ePaper controllers require a hardware reset to leave deep sleep, so
     * this pulses the driver's reset line and re-runs the full controller
     * initialization, restoring the configured color mode. The
     * partial-refresh baseline is lost: the next refresh is promoted to
     * @ref refresh_mode::full.
     *
     * @return Success, or an error.
     * @retval invalid_state No reset line is configured.
     */
    [[nodiscard]] result<void> try_wake() {
        if (auto r = do_wake(); !r) {
            return r;
        }
        _asleep = false;
        _has_baseline = false;
        return {};
    }

protected:
    /**
     * @brief Constructs the panel base with the given dimensions and no
     *        control lines.
     *
     * The default @ref do_wait then returns success immediately; drivers
     * managing BUSY themselves should override it.
     *
     * @param width  Panel width in pixels.
     * @param height Panel height in pixels.
     */
    panel(size_t width, size_t height) noexcept
        : _width(width)
        , _height(height) {}

    /**
     * @brief Constructs the panel base with the given dimensions and
     *        control lines.
     *
     * The base then owns the physical control lines: the default
     * @ref do_wait polls @p busy_gpio, and @ref hardware_reset pulses
     * @p reset_gpio. Configure the pin directions first (see
     * @ref configure_control_lines).
     *
     * @param width        Panel width in pixels.
     * @param height       Panel height in pixels.
     * @param busy_gpio    The BUSY input pin, or `gpio::nc()`.
     * @param busy_level   The level the controller drives while busy.
     * @param busy_timeout Default maximum time to wait for BUSY to release.
     * @param reset_gpio   The reset output pin, or `gpio::nc()`.
     */
    panel(
        size_t width,
        size_t height,
        gpio busy_gpio,
        enum gpio::level busy_level,
        std::chrono::milliseconds busy_timeout,
        gpio reset_gpio
    ) noexcept
        : _width(width)
        , _height(height)
        , _busy_gpio(busy_gpio)
        , _busy_level(busy_level)
        , _busy_timeout(busy_timeout)
        , _reset_gpio(reset_gpio) {}

    panel(panel&&) noexcept = default;
    panel& operator=(panel&&) noexcept = default;

    /** @brief Returns the BUSY input pin (may be unconnected). */
    [[nodiscard]] gpio busy_gpio() const noexcept { return _busy_gpio; }

    /** @brief Returns the reset output pin (may be unconnected). */
    [[nodiscard]] gpio reset_gpio() const noexcept { return _reset_gpio; }

    /** @brief Returns the default BUSY timeout. */
    [[nodiscard]] std::chrono::milliseconds busy_timeout() const noexcept { return _busy_timeout; }

    // =========================================================================
    // Customization hooks
    //
    // Concrete drivers override these (typically privately). Each hook
    // implements the correspondingly named public method; validation common
    // to all controllers (alignment, bounds, sleep state, color-mode match,
    // partial-refresh baseline promotion) has already been performed by the
    // public wrappers.
    // =========================================================================

    /// Hook for @ref try_write / @ref try_write_rows (monochrome). Uploads
    /// rows `[row_start, row_end)` of @p fb with its origin at (@p x, @p y).
    /// The placement is aligned and in-bounds, the row range is valid, and
    /// the panel is awake and in @ref color_mode::mono. Must not reference
    /// @p fb's storage after returning: callers may mutate the framebuffer
    /// immediately.
    [[nodiscard]] virtual result<void>
    do_write(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) = 0;
    /// Hook for @ref try_write / @ref try_write_rows (grayscale). Uploads
    /// rows `[row_start, row_end)` of @p fb with its origin at (@p x, @p y).
    /// The placement is aligned and in-bounds, the row range is valid, and
    /// the panel is awake and in @ref color_mode::gray4. Must not reference
    /// @p fb's storage after returning: callers may mutate the framebuffer
    /// immediately. The default returns `errc::not_supported`; drivers with
    /// grayscale support override it alongside @ref do_set_color_mode
    /// (without that override the panel never enters @ref color_mode::gray4,
    /// so this hook is never reached).
    [[nodiscard]] virtual result<void>
    do_write(const gray4_framebuffer& fb, size_t row_start, size_t row_end, size_t x, size_t y) {
        (void)fb;
        (void)row_start;
        (void)row_end;
        (void)x;
        (void)y;
        return error(errc::not_supported);
    }

    /// Hook for @ref try_clear. Fills the entire image RAM with white in
    /// the current color mode. The panel is awake; the base class
    /// invalidates the partial-refresh baseline after a successful clear.
    [[nodiscard]] virtual result<void> do_clear() = 0;

    /// Hook for @ref try_refresh. The panel is awake, and a `partial`
    /// request has already been promoted to `full` when no baseline exists.
    /// Must block until the update completes (or times out), and must
    /// return `errc::not_supported` for a mode the driver cannot perform
    /// in the current color mode.
    [[nodiscard]] virtual result<void> do_refresh(refresh_mode mode) = 0;

    /// Hook for @ref try_set_color_mode. Called only for an actual mode
    /// change while the panel is awake. The default returns
    /// `errc::not_supported`: drivers with grayscale support override it.
    [[nodiscard]] virtual result<void> do_set_color_mode(enum color_mode mode) {
        (void)mode;
        return error(errc::not_supported);
    }

    /// Hook for @ref try_sleep. Called only while the panel is awake.
    [[nodiscard]] virtual result<void> do_sleep() = 0;
    /// Hook for @ref try_wake. Must hardware-reset the controller,
    /// re-initialize it, and restore the current color mode.
    [[nodiscard]] virtual result<void> do_wake() = 0;

    /// Hook for @ref try_wait / @ref try_wait_for. A `std::nullopt` timeout
    /// selects the driver's configured default. The default implementation
    /// polls the BUSY line configured at construction (see @ref wait_busy).
    [[nodiscard]] virtual result<void> do_wait(std::optional<std::chrono::milliseconds> timeout) {
        return wait_busy(timeout);
    }

    /**
     * @brief Configures the direction of the BUSY and reset pins.
     *
     * Shared helper for driver factories, run before construction: sets
     * @p busy_gpio as an input (it is driven by the panel, so no pull is
     * needed) and, when connected, @p reset_gpio as an output idling high.
     *
     * @param busy_gpio  The BUSY input pin (required).
     * @param reset_gpio The reset output pin, or `gpio::nc()`.
     * @return Success, or an error.
     * @retval invalid_arg @p busy_gpio is not connected.
     */
    [[nodiscard]] static result<void> configure_control_lines(gpio busy_gpio, gpio reset_gpio);

    /**
     * @brief Polls the BUSY line until it releases.
     *
     * Polls the BUSY pin configured at construction once per RTOS tick
     * until it leaves the busy level, reporting `errc::timeout` if it does
     * not release in time. Returns success immediately if no BUSY pin is
     * configured.
     *
     * @param timeout Maximum time to wait, or `std::nullopt` for the
     *                default configured at construction.
     * @return Success, or an error.
     * @retval timeout The BUSY line did not release in time.
     */
    [[nodiscard]] result<void> wait_busy(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /**
     * @brief Pulses the reset line and waits for the controller to settle.
     *
     * Drives the reset pin configured at construction low for 10 ms, back
     * high for 10 ms, then waits for the BUSY line to release.
     *
     * @return Success, or an error.
     * @retval invalid_state No reset pin is configured.
     * @retval timeout The BUSY line did not release after the reset.
     */
    [[nodiscard]] result<void> hardware_reset();

private:
    template<typename FB>
    [[nodiscard]] result<void>
    _try_write_rows(const FB& fb, enum color_mode expected, size_t row_start, size_t row_end, size_t x, size_t y);

    size_t _width;
    size_t _height;
    gpio _busy_gpio = gpio::nc();
    enum gpio::level _busy_level = gpio::level::high;
    std::chrono::milliseconds _busy_timeout{15'000};
    gpio _reset_gpio = gpio::nc();
    enum color_mode _color_mode = color_mode::mono;
    bool _asleep = false;
    // Whether the controller holds a refreshed image a partial update can
    // diff against; cleared by wake and color-mode changes.
    bool _has_baseline = false;
};

} // namespace idfxx::epaper

/** @} */ // end of idfxx_epaper
