// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/panel>
 * @file panel.hpp
 * @brief Abstract LCD panel interface.
 *
 * @defgroup idfxx_lcd LCD Component
 * @brief LCD panel interface, color types, and framebuffers.
 *
 * Provides the abstract panel base class, color types, and framebuffer
 * helpers for LCD and OLED displays. The I/O layer for SPI- and
 * I2C-connected panels is provided by @ref idfxx_panel_io.
 *
 * Depends on @ref idfxx_core for error handling and @ref idfxx_gpio for pin
 * configuration.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error>
#include <idfxx/gpio>
#include <idfxx/lcd/color>

#include <cstddef>

struct esp_lcd_panel_t;
typedef struct esp_lcd_panel_t* esp_lcd_panel_handle_t;

/**
 * @headerfile <idfxx/lcd/panel>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

class mono_framebuffer;
class rgb565_framebuffer;

/**
 * @headerfile <idfxx/lcd/panel>
 * @brief Abstract base class for LCD panels.
 *
 * The public interface is non-virtual; concrete drivers (e.g.
 * `idfxx::lcd::ili9341`) customize behaviour by overriding the protected
 * `do_*` hooks, mirroring the standard library's non-virtual-interface
 * pattern (cf. `std::pmr::memory_resource`).
 *
 * Framebuffer contents reach the display through @ref write (the whole
 * framebuffer), @ref write_rows (a horizontal band) and @ref write_region
 * (a rectangle), each placing the framebuffer's origin at a destination
 * pixel. A write does not retain a reference to the framebuffer: it may be
 * redrawn immediately after the call returns.
 *
 * @code
 * idfxx::lcd::rgb565_framebuffer fb(display.width(), display.height());
 * fb.fill(idfxx::lcd::rgb565(0, 0, 255));
 * fb.set_pixel(10, 20, idfxx::lcd::rgb565(255, 255, 255));
 * display.write(fb);
 * @endcode
 */
class panel {
public:
    /**
     * @headerfile <idfxx/lcd/panel>
     * @brief Configuration structure for LCD panels.
     */
    struct config {
        gpio reset_gpio = gpio::nc(); ///< GPIO number for hardware reset, or idfxx::gpio::nc() if not used
        enum rgb_element_order rgb_element_order = rgb_element_order::rgb; ///< Set RGB element order, RGB or BGR
        rgb_data_endian data_endian = rgb_data_endian::big; ///< Set the data endian for color data larger than 1 byte
        uint32_t bits_per_pixel = 16;                       ///< Color depth, in bpp
        gpio::level reset_active_level = gpio::level::low;  ///< Active level for the panel reset signal
        void* vendor_config = nullptr; ///< vendor specific configuration, optional, left as NULL if not used
    };

    virtual ~panel() = default;

    panel(const panel&) = delete;
    panel& operator=(const panel&) = delete;

    /**
     * @brief Returns the underlying ESP-IDF handle.
     * @return The ESP-IDF LCD panel handle.
     */
    [[nodiscard]] esp_lcd_panel_handle_t idf_handle() const { return do_idf_handle(); }

    /**
     * @brief Returns the panel's native width in pixels.
     *
     * The native dimensions are fixed at construction and are not affected by
     * @ref swap_xy or @ref mirror.
     *
     * @return The width in pixels, or 0 if the driver does not report dimensions.
     */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /**
     * @brief Returns the panel's native height in pixels.
     *
     * The native dimensions are fixed at construction and are not affected by
     * @ref swap_xy or @ref mirror.
     *
     * @return The height in pixels, or 0 if the driver does not report dimensions.
     */
    [[nodiscard]] size_t height() const noexcept { return _height; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Draws an RGB565 framebuffer to the display.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y), so a band-sized
     * framebuffer can render a taller frame in slices. The panel must use
     * the RGB565 format.
     *
     * @param fb The framebuffer to draw.
     * @param x  Destination column of the framebuffer's left edge.
     * @param y  Destination row of the framebuffer's top edge.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void write(const rgb565_framebuffer& fb, size_t x = 0, size_t y = 0) { unwrap(try_write(fb, x, y)); }

    /**
     * @brief Draws a monochrome framebuffer to the display.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y). The panel must
     * use the page-packed 1-bpp format (e.g. an SSD1306).
     *
     * @param fb The framebuffer to draw.
     * @param x  Destination column of the framebuffer's left edge.
     * @param y  Destination row of the framebuffer's top edge; must be a
     *           multiple of 8 (the page-packed layout groups rows into
     *           8-row pages).
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error, including `errc::invalid_arg` for
     *         an unaligned @p y.
     */
    void write(const mono_framebuffer& fb, size_t x = 0, size_t y = 0) { unwrap(try_write(fb, x, y)); }

    /**
     * @brief Draws a horizontal band of an RGB565 framebuffer to the display.
     *
     * Draws rows `[row_start, row_end)` of @p fb across its full width, with
     * the framebuffer's origin at pixel (@p x, @p y) — row `r` of the
     * framebuffer lands at display row `y + r`, so a band written from a
     * full-frame framebuffer lands exactly where the full write would place
     * it. The band transfers in a single draw. The panel must use the RGB565
     * format.
     *
     * @param fb        The framebuffer to draw from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin.
     * @param y         Destination row of the framebuffer origin.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error, including `errc::invalid_arg` for
     *         an invalid row range.
     */
    void write_rows(const rgb565_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0) {
        unwrap(try_write_rows(fb, row_start, row_end, x, y));
    }

    /**
     * @brief Draws a horizontal band of a monochrome framebuffer to the display.
     *
     * Draws rows `[row_start, row_end)` of @p fb across its full width, with
     * the framebuffer's origin at pixel (@p x, @p y) — row `r` of the
     * framebuffer lands at display row `y + r`, so a band written from a
     * full-frame framebuffer lands exactly where the full write would place
     * it. The rows are expanded outward to page boundaries (multiples of 8)
     * to match the page-packed layout, and the band transfers in a single
     * draw. The panel must use the page-packed 1-bpp format (e.g. an
     * SSD1306).
     *
     * @param fb        The framebuffer to draw from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin.
     * @param y         Destination row of the framebuffer origin; must be a
     *                  multiple of 8.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error, including `errc::invalid_arg` for
     *         an invalid row range or an unaligned @p y.
     */
    void write_rows(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0) {
        unwrap(try_write_rows(fb, row_start, row_end, x, y));
    }

    /**
     * @brief Draws a rectangular region of an RGB565 framebuffer to the display.
     *
     * Draws columns `[x_start, x_end)` and rows `[y_start, y_end)` of @p fb,
     * with the framebuffer's origin at pixel (@p x, @p y) — framebuffer pixel
     * `(c, r)` lands at display pixel `(x + c, y + r)`. Full-width regions
     * transfer in a single draw; narrower regions transfer one draw per row.
     * The panel must use the RGB565 format.
     *
     * @param fb      The framebuffer to draw from.
     * @param x_start First framebuffer column of the region, inclusive.
     * @param y_start First framebuffer row of the region, inclusive.
     * @param x_end   End framebuffer column, exclusive; must satisfy
     *                `x_start < x_end <= fb.width()`.
     * @param y_end   End framebuffer row, exclusive; must satisfy
     *                `y_start < y_end <= fb.height()`.
     * @param x       Destination column of the framebuffer origin.
     * @param y       Destination row of the framebuffer origin.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error, including `errc::invalid_arg` for
     *         an invalid region.
     */
    void write_region(
        const rgb565_framebuffer& fb,
        size_t x_start,
        size_t y_start,
        size_t x_end,
        size_t y_end,
        size_t x = 0,
        size_t y = 0
    ) {
        unwrap(try_write_region(fb, x_start, y_start, x_end, y_end, x, y));
    }

    /**
     * @brief Draws a rectangular region of a monochrome framebuffer to the display.
     *
     * Draws columns `[x_start, x_end)` and rows `[y_start, y_end)` of @p fb,
     * with the framebuffer's origin at pixel (@p x, @p y) — framebuffer pixel
     * `(c, r)` lands at display pixel `(x + c, y + r)`. The rows are expanded
     * outward to page boundaries (multiples of 8) to match the page-packed
     * layout. Full-width regions transfer in a single draw; narrower regions
     * transfer one draw per page. The panel must use the page-packed 1-bpp
     * format (e.g. an SSD1306).
     *
     * @param fb      The framebuffer to draw from.
     * @param x_start First framebuffer column of the region, inclusive.
     * @param y_start First framebuffer row of the region, inclusive.
     * @param x_end   End framebuffer column, exclusive; must satisfy
     *                `x_start < x_end <= fb.width()`.
     * @param y_end   End framebuffer row, exclusive; must satisfy
     *                `y_start < y_end <= fb.height()`.
     * @param x       Destination column of the framebuffer origin.
     * @param y       Destination row of the framebuffer origin; must be a
     *                multiple of 8.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error, including `errc::invalid_arg` for
     *         an invalid region or an unaligned @p y.
     */
    void write_region(
        const mono_framebuffer& fb,
        size_t x_start,
        size_t y_start,
        size_t x_end,
        size_t y_end,
        size_t x = 0,
        size_t y = 0
    ) {
        unwrap(try_write_region(fb, x_start, y_start, x_end, y_end, x, y));
    }
#endif

    /**
     * @brief Draws an RGB565 framebuffer to the display.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y), so a band-sized
     * framebuffer can render a taller frame in slices. The panel must use
     * the RGB565 format.
     *
     * @param fb The framebuffer to draw.
     * @param x  Destination column of the framebuffer's left edge.
     * @param y  Destination row of the framebuffer's top edge.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_write(const rgb565_framebuffer& fb, size_t x = 0, size_t y = 0);

    /**
     * @brief Draws a monochrome framebuffer to the display.
     *
     * Places the framebuffer's origin at pixel (@p x, @p y). The panel must
     * use the page-packed 1-bpp format (e.g. an SSD1306).
     *
     * @param fb The framebuffer to draw.
     * @param x  Destination column of the framebuffer's left edge.
     * @param y  Destination row of the framebuffer's top edge; must be a
     *           multiple of 8 (the page-packed layout groups rows into
     *           8-row pages).
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if @p y is not a multiple of 8.
     */
    [[nodiscard]] result<void> try_write(const mono_framebuffer& fb, size_t x = 0, size_t y = 0);

    /**
     * @brief Draws a horizontal band of an RGB565 framebuffer to the display.
     *
     * Draws rows `[row_start, row_end)` of @p fb across its full width, with
     * the framebuffer's origin at pixel (@p x, @p y) — row `r` of the
     * framebuffer lands at display row `y + r`, so a band written from a
     * full-frame framebuffer lands exactly where the full write would place
     * it. The band transfers in a single draw. The panel must use the RGB565
     * format.
     *
     * @param fb        The framebuffer to draw from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin.
     * @param y         Destination row of the framebuffer origin.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if the row range is invalid.
     */
    [[nodiscard]] result<void>
    try_write_rows(const rgb565_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0);

    /**
     * @brief Draws a horizontal band of a monochrome framebuffer to the display.
     *
     * Draws rows `[row_start, row_end)` of @p fb across its full width, with
     * the framebuffer's origin at pixel (@p x, @p y) — row `r` of the
     * framebuffer lands at display row `y + r`, so a band written from a
     * full-frame framebuffer lands exactly where the full write would place
     * it. The rows are expanded outward to page boundaries (multiples of 8)
     * to match the page-packed layout, and the band transfers in a single
     * draw. The panel must use the page-packed 1-bpp format (e.g. an
     * SSD1306).
     *
     * @param fb        The framebuffer to draw from.
     * @param row_start First framebuffer row of the band, inclusive.
     * @param row_end   End framebuffer row, exclusive; must satisfy
     *                  `row_start < row_end <= fb.height()`.
     * @param x         Destination column of the framebuffer origin.
     * @param y         Destination row of the framebuffer origin; must be a
     *                  multiple of 8.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if the row range is invalid, or @p y
     *         is not a multiple of 8.
     */
    [[nodiscard]] result<void>
    try_write_rows(const mono_framebuffer& fb, size_t row_start, size_t row_end, size_t x = 0, size_t y = 0);

    /**
     * @brief Draws a rectangular region of an RGB565 framebuffer to the display.
     *
     * Draws columns `[x_start, x_end)` and rows `[y_start, y_end)` of @p fb,
     * with the framebuffer's origin at pixel (@p x, @p y) — framebuffer pixel
     * `(c, r)` lands at display pixel `(x + c, y + r)`. Full-width regions
     * transfer in a single draw; narrower regions transfer one draw per row.
     * The panel must use the RGB565 format.
     *
     * @param fb      The framebuffer to draw from.
     * @param x_start First framebuffer column of the region, inclusive.
     * @param y_start First framebuffer row of the region, inclusive.
     * @param x_end   End framebuffer column, exclusive; must satisfy
     *                `x_start < x_end <= fb.width()`.
     * @param y_end   End framebuffer row, exclusive; must satisfy
     *                `y_start < y_end <= fb.height()`.
     * @param x       Destination column of the framebuffer origin.
     * @param y       Destination row of the framebuffer origin.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if the region is invalid.
     */
    [[nodiscard]] result<void> try_write_region(
        const rgb565_framebuffer& fb,
        size_t x_start,
        size_t y_start,
        size_t x_end,
        size_t y_end,
        size_t x = 0,
        size_t y = 0
    );

    /**
     * @brief Draws a rectangular region of a monochrome framebuffer to the display.
     *
     * Draws columns `[x_start, x_end)` and rows `[y_start, y_end)` of @p fb,
     * with the framebuffer's origin at pixel (@p x, @p y) — framebuffer pixel
     * `(c, r)` lands at display pixel `(x + c, y + r)`. The rows are expanded
     * outward to page boundaries (multiples of 8) to match the page-packed
     * layout. Full-width regions transfer in a single draw; narrower regions
     * transfer one draw per page. The panel must use the page-packed 1-bpp
     * format (e.g. an SSD1306).
     *
     * @param fb      The framebuffer to draw from.
     * @param x_start First framebuffer column of the region, inclusive.
     * @param y_start First framebuffer row of the region, inclusive.
     * @param x_end   End framebuffer column, exclusive; must satisfy
     *                `x_start < x_end <= fb.width()`.
     * @param y_end   End framebuffer row, exclusive; must satisfy
     *                `y_start < y_end <= fb.height()`.
     * @param x       Destination column of the framebuffer origin.
     * @param y       Destination row of the framebuffer origin; must be a
     *                multiple of 8.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if the region is invalid, or @p y is
     *         not a multiple of 8.
     */
    [[nodiscard]] result<void> try_write_region(
        const mono_framebuffer& fb,
        size_t x_start,
        size_t y_start,
        size_t x_end,
        size_t y_end,
        size_t x = 0,
        size_t y = 0
    );

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Draws bitmap data to a region of the display.
     *
     * The region is end-exclusive: it spans columns `[x_start, x_end)` and rows
     * `[y_start, y_end)`. The buffer layout of @p color_data is panel-specific
     * (e.g. RGB565 pixels for color panels, page-packed 1-bpp data for
     * monochrome OLEDs); consult the concrete driver's documentation.
     *
     * @param x_start    Start column, inclusive.
     * @param y_start    Start row, inclusive.
     * @param x_end      End column, exclusive.
     * @param y_end      End row, exclusive.
     * @param color_data Pixel data in the panel's native format.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data) {
        unwrap(try_draw_bitmap(x_start, y_start, x_end, y_end, color_data));
    }

    /**
     * @brief Inverts the color of the display.
     * @param invert true to invert colors, false for normal.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void invert_color(bool invert) { unwrap(try_invert_color(invert)); }

    /**
     * @brief Swaps the X and Y axes.
     * @param swap true to swap, false for normal.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void swap_xy(bool swap) { unwrap(try_swap_xy(swap)); }

    /**
     * @brief Mirrors the display.
     * @param mirror_x true to mirror horizontally.
     * @param mirror_y true to mirror vertically.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void mirror(bool mirror_x, bool mirror_y) { unwrap(try_mirror(mirror_x, mirror_y)); }

    /**
     * @brief Turns the display on or off.
     * @param on true to turn on, false to turn off.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void display_on(bool on) { unwrap(try_display_on(on)); }
#endif

    /**
     * @brief Draws bitmap data to a region of the display.
     *
     * The region is end-exclusive: it spans columns `[x_start, x_end)` and rows
     * `[y_start, y_end)`. The buffer layout of @p color_data is panel-specific
     * (e.g. RGB565 pixels for color panels, page-packed 1-bpp data for
     * monochrome OLEDs); consult the concrete driver's documentation.
     *
     * @param x_start    Start column, inclusive.
     * @param y_start    Start row, inclusive.
     * @param x_end      End column, exclusive.
     * @param y_end      End row, exclusive.
     * @param color_data Pixel data in the panel's native format.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data) {
        return do_draw_bitmap(x_start, y_start, x_end, y_end, color_data);
    }

    /**
     * @brief Inverts the color of the display.
     * @param invert true to invert colors, false for normal.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_invert_color(bool invert) { return do_invert_color(invert); }

    /**
     * @brief Swaps the X and Y axes.
     * @param swap true to swap, false for normal.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_swap_xy(bool swap) { return do_swap_xy(swap); }

    /**
     * @brief Mirrors the display.
     * @param mirror_x true to mirror horizontally.
     * @param mirror_y true to mirror vertically.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_mirror(bool mirror_x, bool mirror_y) { return do_mirror(mirror_x, mirror_y); }

    /**
     * @brief Turns the display on or off.
     * @param on true to turn on, false to turn off.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_display_on(bool on) { return do_display_on(on); }

protected:
    panel() = default;
    /// Constructs a panel reporting the given native dimensions.
    panel(size_t width, size_t height) noexcept
        : _width(width)
        , _height(height) {}
    panel(panel&&) noexcept = default;
    panel& operator=(panel&&) noexcept = default;

    // =========================================================================
    // Customization hooks
    //
    // Concrete drivers override these (typically privately). Each hook
    // implements the correspondingly named public method.
    // =========================================================================

    /// Hook for @ref idf_handle.
    [[nodiscard]] virtual esp_lcd_panel_handle_t do_idf_handle() const = 0;
    /// Hook for @ref try_draw_bitmap. The default implementation draws via the
    /// panel handle returned by do_idf_handle().
    [[nodiscard]] virtual result<void>
    do_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data);
    /// Hook for @ref try_invert_color. The default implementation inverts via
    /// the panel handle returned by do_idf_handle().
    [[nodiscard]] virtual result<void> do_invert_color(bool invert);
    /// Hook for @ref try_swap_xy. The default implementation swaps via the
    /// panel handle returned by do_idf_handle().
    [[nodiscard]] virtual result<void> do_swap_xy(bool swap);
    /// Hook for @ref try_mirror. The default implementation mirrors via the
    /// panel handle returned by do_idf_handle().
    [[nodiscard]] virtual result<void> do_mirror(bool mirror_x, bool mirror_y);
    /// Hook for @ref try_display_on. The default implementation switches the
    /// display via the panel handle returned by do_idf_handle().
    [[nodiscard]] virtual result<void> do_display_on(bool on);

private:
    size_t _width = 0;
    size_t _height = 0;
};

/** @} */ // end of idfxx_lcd

} // namespace idfxx::lcd
