// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/rgb565_framebuffer>
 * @file rgb565_framebuffer.hpp
 * @brief Framebuffer for RGB565 color displays.
 * @ingroup idfxx_lcd
 */

#include <idfxx/error>
#include <idfxx/lcd/color>
#include <idfxx/lcd/panel>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

/**
 * @headerfile <idfxx/lcd/rgb565_framebuffer>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/rgb565_framebuffer>
 * @brief In-memory framebuffer for RGB565 (16 bits per pixel) color displays.
 *
 * Pixels are stored row-major as @ref rgb565 values (panel byte order), so
 * the buffer can be passed directly to a color panel's draw_bitmap. The
 * pixel (x, y) is at index `y * width() + x`.
 *
 * Draw into the framebuffer with @ref set_pixel and friends, then push it to
 * a panel with @ref flush. A full frame at 16 bpp is large (a 240x320 panel
 * needs 150 KB), so the framebuffer may also be sized as a horizontal band
 * and flushed at a destination offset, rendering the frame in slices:
 *
 * @code
 * idfxx::lcd::rgb565_framebuffer band(display.width(), 40);
 * for (size_t y = 0; y < display.height(); y += band.height()) {
 *     band.fill({0, 0, 0});
 *     // ... draw the slice covering rows [y, y + band.height()) ...
 *     band.flush(display, 0, y);
 * }
 * @endcode
 *
 * This is a plain value type: copyable, movable, and independent of any
 * panel.
 */
class rgb565_framebuffer {
public:
    /** @brief The value type written by @ref set_pixel. */
    using pixel_type = rgb565;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels black.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error (e.g. invalid dimensions).
     */
    [[nodiscard]] rgb565_framebuffer(size_t width, size_t height)
        : rgb565_framebuffer(unwrap(make(width, height))) {}
#endif

    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels black.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero.
     *
     * @return The new rgb565_framebuffer, or an error.
     * @retval idfxx::errc::invalid_arg if @p width or @p height is zero.
     */
    [[nodiscard]] static result<rgb565_framebuffer> make(size_t width, size_t height) {
        if (width == 0 || height == 0) {
            return error(errc::invalid_arg);
        }
        return rgb565_framebuffer{width, height, std::vector<rgb565>(width * height)};
    }

    /** @brief Returns the width in pixels. */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /** @brief Returns the height in pixels. */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Sets a single pixel to the given color.
     *
     * Out-of-range coordinates are ignored.
     *
     * @param x     Column, in `[0, width())`.
     * @param y     Row, in `[0, height())`.
     * @param color The color to write.
     */
    void set_pixel(size_t x, size_t y, rgb565 color) noexcept {
        if (x >= _width || y >= _height) {
            return;
        }
        _data[y * _width + x] = color;
    }

    /**
     * @brief Returns the color of a single pixel.
     *
     * @param x Column, in `[0, width())`.
     * @param y Row, in `[0, height())`.
     * @return The pixel's color, or black if the coordinates are out of range.
     */
    [[nodiscard]] rgb565 get_pixel(size_t x, size_t y) const noexcept {
        if (x >= _width || y >= _height) {
            return {};
        }
        return _data[y * _width + x];
    }

    /**
     * @brief Sets every pixel to the given color.
     * @param color The color to fill with.
     */
    void fill(rgb565 color) noexcept { std::ranges::fill(_data, color); }

    /** @brief Sets every pixel to black (equivalent to `fill({})`). */
    void clear() noexcept { fill({}); }

    /**
     * @brief Returns the raw pixel data.
     *
     * The span holds `width() * height()` values in the row-major layout
     * described in the class documentation, suitable for passing directly to
     * a color panel's draw_bitmap.
     *
     * @return A read-only view of the pixel data.
     */
    [[nodiscard]] std::span<const rgb565> data() const noexcept { return _data; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Draws the full framebuffer to a panel.
     *
     * The framebuffer's top-left corner lands at (@p x, @p y) on the panel,
     * so a band-sized framebuffer can render a taller frame in slices. The
     * panel must use the RGB565 format.
     *
     * @param panel The panel to draw to.
     * @param x     Destination column of the framebuffer's left edge.
     * @param y     Destination row of the framebuffer's top edge.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void flush(panel& panel, size_t x = 0, size_t y = 0) const { unwrap(try_flush(panel, x, y)); }

    /**
     * @brief Draws a horizontal band of the framebuffer to a panel.
     *
     * The band spans rows `[y_start, y_end)` across the full width, and is
     * drawn to the same rows on the panel. The panel must use the RGB565
     * format and should match the framebuffer's dimensions.
     *
     * @param panel   The panel to draw to.
     * @param y_start First row of the band, inclusive.
     * @param y_end   End row of the band, exclusive; must satisfy `y_start < y_end <= height()`.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error (e.g. an invalid row range).
     */
    void flush_rows(panel& panel, size_t y_start, size_t y_end) const { unwrap(try_flush_rows(panel, y_start, y_end)); }

    /**
     * @brief Draws a rectangular region of the framebuffer to a panel.
     *
     * The region spans columns `[x_start, x_end)` and rows `[y_start, y_end)`,
     * and is drawn to the same coordinates on the panel. Full-width regions
     * transfer in a single draw; narrower regions transfer one draw per row.
     * The panel must use the RGB565 format and should match the
     * framebuffer's dimensions.
     *
     * @param panel   The panel to draw to.
     * @param x_start First column of the region, inclusive.
     * @param y_start First row of the region, inclusive.
     * @param x_end   End column, exclusive; must satisfy `x_start < x_end <= width()`.
     * @param y_end   End row, exclusive; must satisfy `y_start < y_end <= height()`.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error (e.g. an invalid region).
     */
    void flush_region(panel& panel, size_t x_start, size_t y_start, size_t x_end, size_t y_end) const {
        unwrap(try_flush_region(panel, x_start, y_start, x_end, y_end));
    }
#endif

    /**
     * @brief Draws the full framebuffer to a panel.
     *
     * The framebuffer's top-left corner lands at (@p x, @p y) on the panel,
     * so a band-sized framebuffer can render a taller frame in slices. The
     * panel must use the RGB565 format.
     *
     * @param panel The panel to draw to.
     * @param x     Destination column of the framebuffer's left edge.
     * @param y     Destination row of the framebuffer's top edge.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_flush(panel& panel, size_t x = 0, size_t y = 0) const {
        return panel.try_draw_bitmap(
            static_cast<int>(x),
            static_cast<int>(y),
            static_cast<int>(x + _width),
            static_cast<int>(y + _height),
            _data.data()
        );
    }

    /**
     * @brief Draws a horizontal band of the framebuffer to a panel.
     *
     * The band spans rows `[y_start, y_end)` across the full width, and is
     * drawn to the same rows on the panel. The panel must use the RGB565
     * format and should match the framebuffer's dimensions.
     *
     * @param panel   The panel to draw to.
     * @param y_start First row of the band, inclusive.
     * @param y_end   End row of the band, exclusive; must satisfy `y_start < y_end <= height()`.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if the row range is invalid.
     */
    [[nodiscard]] result<void> try_flush_rows(panel& panel, size_t y_start, size_t y_end) const {
        return try_flush_region(panel, 0, y_start, _width, y_end);
    }

    /**
     * @brief Draws a rectangular region of the framebuffer to a panel.
     *
     * The region spans columns `[x_start, x_end)` and rows `[y_start, y_end)`,
     * and is drawn to the same coordinates on the panel. Full-width regions
     * transfer in a single draw; narrower regions transfer one draw per row.
     * The panel must use the RGB565 format and should match the
     * framebuffer's dimensions.
     *
     * @param panel   The panel to draw to.
     * @param x_start First column of the region, inclusive.
     * @param y_start First row of the region, inclusive.
     * @param x_end   End column, exclusive; must satisfy `x_start < x_end <= width()`.
     * @param y_end   End row, exclusive; must satisfy `y_start < y_end <= height()`.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if the region is invalid.
     */
    [[nodiscard]] result<void>
    try_flush_region(panel& panel, size_t x_start, size_t y_start, size_t x_end, size_t y_end) const {
        if (x_start >= x_end || x_end > _width || y_start >= y_end || y_end > _height) {
            return error(errc::invalid_arg);
        }
        if (x_start == 0 && x_end == _width) {
            // In the row-major layout a full-width band is contiguous, so a
            // single transfer covers all its rows.
            return panel.try_draw_bitmap(
                0,
                static_cast<int>(y_start),
                static_cast<int>(_width),
                static_cast<int>(y_end),
                _data.data() + y_start * _width
            );
        }
        for (size_t row = y_start; row < y_end; ++row) {
            auto drawn = panel.try_draw_bitmap(
                static_cast<int>(x_start),
                static_cast<int>(row),
                static_cast<int>(x_end),
                static_cast<int>(row + 1),
                _data.data() + row * _width + x_start
            );
            if (!drawn) {
                return drawn;
            }
        }
        return {};
    }

private:
    rgb565_framebuffer(size_t width, size_t height, std::vector<rgb565> data)
        : _width(width)
        , _height(height)
        , _data(std::move(data)) {}

    size_t _width;
    size_t _height;
    std::vector<rgb565> _data;
};

} // namespace idfxx::lcd
