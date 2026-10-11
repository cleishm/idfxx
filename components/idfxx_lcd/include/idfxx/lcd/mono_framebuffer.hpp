// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/mono_framebuffer>
 * @file mono_framebuffer.hpp
 * @brief Framebuffer for monochrome (1-bpp) displays.
 * @ingroup idfxx_lcd
 */

#include "sdkconfig.h"

#include <idfxx/error>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

/**
 * @headerfile <idfxx/lcd/mono_framebuffer>
 * @brief LCD driver classes.
 */
namespace idfxx::lcd {

/**
 * @headerfile <idfxx/lcd/mono_framebuffer>
 * @brief In-memory framebuffer for monochrome (1 bit per pixel) displays.
 *
 * Pixels are stored page-packed, the native format of SSD1306-style
 * monochrome OLED controllers: each byte holds 8 vertically adjacent pixels
 * (bit 0 is the topmost), and pages (rows of 8 pixels) are laid out
 * top-to-bottom, each page spanning the full display width. The byte for
 * pixel (x, y) is at index `(y / 8) * width() + x`, bit `y % 8`.
 *
 * Draw into the framebuffer with @ref set_pixel and friends, then draw it on
 * a panel with @ref panel::write (full frame), @ref panel::write_rows (a
 * horizontal band) or @ref panel::write_region (a rectangle). This is a
 * plain value type: copyable, movable, and independent of any panel.
 *
 * @code
 * idfxx::lcd::mono_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, true);
 * display.write(fb);
 * @endcode
 */
class mono_framebuffer {
public:
    /** @brief The value type written by @ref set_pixel. */
    using pixel_type = bool;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels off.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero and a multiple of 8.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error (e.g. invalid dimensions).
     */
    [[nodiscard]] mono_framebuffer(size_t width, size_t height)
        : mono_framebuffer(unwrap(make(width, height))) {}
#endif

    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels off.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero and a multiple of 8.
     *
     * @return The new mono_framebuffer, or an error.
     * @retval idfxx::errc::invalid_arg if @p width is zero, or @p height is zero or not a multiple of 8.
     */
    [[nodiscard]] static result<mono_framebuffer> make(size_t width, size_t height) {
        if (width == 0 || height == 0 || height % 8 != 0) {
            return error(errc::invalid_arg);
        }
        return mono_framebuffer{width, height, std::vector<uint8_t>(width * height / 8)};
    }

    /** @brief Returns the width in pixels. */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /** @brief Returns the height in pixels. */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Sets or clears a single pixel.
     *
     * Out-of-range coordinates are ignored.
     *
     * @param x  Column, in `[0, width())`.
     * @param y  Row, in `[0, height())`.
     * @param on true to set the pixel, false to clear it.
     */
    void set_pixel(size_t x, size_t y, bool on) noexcept {
        if (x >= _width || y >= _height) {
            return;
        }
        uint8_t& byte = _data[(y / 8) * _width + x];
        uint8_t mask = static_cast<uint8_t>(1u << (y % 8));
        if (on) {
            byte |= mask;
        } else {
            byte &= static_cast<uint8_t>(~mask);
        }
    }

    /**
     * @brief Returns the state of a single pixel.
     *
     * @param x Column, in `[0, width())`.
     * @param y Row, in `[0, height())`.
     * @return true if the pixel is set; false if it is clear or the coordinates are out of range.
     */
    [[nodiscard]] bool get_pixel(size_t x, size_t y) const noexcept {
        if (x >= _width || y >= _height) {
            return false;
        }
        return (_data[(y / 8) * _width + x] & (1u << (y % 8))) != 0;
    }

    /**
     * @brief Sets every pixel to the given state.
     * @param on true to set all pixels, false to clear them.
     */
    void fill(bool on) noexcept { std::ranges::fill(_data, on ? uint8_t{0xFF} : uint8_t{0x00}); }

    /** @brief Clears every pixel (equivalent to `fill(false)`). */
    void clear() noexcept { fill(false); }

    /**
     * @brief Returns the raw page-packed pixel data.
     *
     * The span holds `width() * height() / 8` bytes in the layout described in
     * the class documentation, suitable for passing directly to a monochrome
     * panel's draw_bitmap.
     *
     * @return A read-only view of the pixel data.
     */
    [[nodiscard]] std::span<const uint8_t> data() const noexcept { return _data; }

private:
    mono_framebuffer(size_t width, size_t height, std::vector<uint8_t> data)
        : _width(width)
        , _height(height)
        , _data(std::move(data)) {}

    size_t _width;
    size_t _height;
    std::vector<uint8_t> _data;
};

} // namespace idfxx::lcd
