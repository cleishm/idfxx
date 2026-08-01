// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/mono_framebuffer>
 * @file mono_framebuffer.hpp
 * @brief Framebuffer for monochrome (1-bpp) ePaper displays.
 * @ingroup idfxx_epaper
 */

#include <idfxx/epaper/panel>
#include <idfxx/error>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/mono_framebuffer>
 * @brief In-memory framebuffer for monochrome (1 bit per pixel) ePaper displays.
 *
 * Pixels are stored row-major with byte-padded rows, the native format of
 * SSD1680- and UC8179-style ePaper controllers: each byte holds 8
 * horizontally adjacent pixels (bit 7 is the leftmost), rows are laid out
 * top-to-bottom, and each row is padded to a whole number of bytes
 * (@ref stride_bytes). The byte for pixel (x, y) is at index
 * `y * stride_bytes() + x / 8`, bit `7 - x % 8`. Following the controllers'
 * RAM convention, a **set bit is white paper** and a cleared bit is black
 * ink; a new framebuffer starts all-white, and `set_pixel(x, y, true)`
 * inks a pixel black.
 *
 * Draw into the framebuffer with @ref set_pixel and friends, then upload it
 * to a panel's RAM with @ref flush (full frame) or @ref flush_rows (a
 * horizontal band), and make it visible with @ref panel::refresh. This is a
 * plain value type: copyable, movable, and independent of any panel.
 *
 * @code
 * idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, true);
 * fb.flush(display);
 * display.refresh();
 * @endcode
 */
class mono_framebuffer {
public:
    /** @brief The value type written by @ref set_pixel — true is black ink. */
    using pixel_type = bool;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels white.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error (e.g. invalid dimensions).
     */
    [[nodiscard]] mono_framebuffer(size_t width, size_t height)
        : mono_framebuffer(unwrap(make(width, height))) {}
#endif

    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels white.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero.
     *
     * @return The new mono_framebuffer, or an error.
     * @retval idfxx::errc::invalid_arg if @p width or @p height is zero.
     */
    [[nodiscard]] static result<mono_framebuffer> make(size_t width, size_t height) {
        if (width == 0 || height == 0) {
            return error(errc::invalid_arg);
        }
        return mono_framebuffer{width, height, std::vector<uint8_t>(((width + 7) / 8) * height, 0xFF)};
    }

    /** @brief Returns the width in pixels. */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /** @brief Returns the height in pixels. */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Returns the number of bytes per row.
     *
     * Rows are padded to whole bytes: `(width() + 7) / 8`. Padding bits sit
     * past the right edge and stay white.
     *
     * @return The row stride, in bytes.
     */
    [[nodiscard]] size_t stride_bytes() const noexcept { return (_width + 7) / 8; }

    /**
     * @brief Inks or blanks a single pixel.
     *
     * Out-of-range coordinates are ignored.
     *
     * @param x   Column, in `[0, width())`.
     * @param y   Row, in `[0, height())`.
     * @param ink true for black ink, false for white paper.
     */
    void set_pixel(size_t x, size_t y, bool ink) noexcept {
        if (x >= _width || y >= _height) {
            return;
        }
        uint8_t& byte = _data[y * stride_bytes() + x / 8];
        uint8_t mask = static_cast<uint8_t>(1u << (7 - x % 8));
        // Controller RAM convention: a set bit is white, a cleared bit is ink.
        if (ink) {
            byte &= static_cast<uint8_t>(~mask);
        } else {
            byte |= mask;
        }
    }

    /**
     * @brief Returns the state of a single pixel.
     *
     * @param x Column, in `[0, width())`.
     * @param y Row, in `[0, height())`.
     * @return true if the pixel is black ink; false if it is white or the
     *         coordinates are out of range.
     */
    [[nodiscard]] bool get_pixel(size_t x, size_t y) const noexcept {
        if (x >= _width || y >= _height) {
            return false;
        }
        return (_data[y * stride_bytes() + x / 8] & (1u << (7 - x % 8))) == 0;
    }

    /**
     * @brief Sets every pixel to the given state.
     * @param ink true to ink all pixels black, false to blank them white.
     */
    void fill(bool ink) noexcept { std::ranges::fill(_data, ink ? uint8_t{0x00} : uint8_t{0xFF}); }

    /** @brief Blanks every pixel to white (equivalent to `fill(false)`). */
    void clear() noexcept { fill(false); }

    /**
     * @brief Returns the raw row-major pixel data.
     *
     * The span holds `stride_bytes() * height()` bytes in the layout
     * described in the class documentation, suitable for streaming directly
     * to an ePaper controller's RAM.
     *
     * @return A read-only view of the pixel data.
     */
    [[nodiscard]] std::span<const uint8_t> data() const noexcept { return _data; }

    /**
     * @brief Returns the raw bytes of a single row.
     *
     * Unlike @ref set_pixel / @ref get_pixel, the raw views do not tolerate
     * out-of-range arguments: passing one is undefined behavior.
     *
     * @param y Row, in `[0, height())`.
     * @return A read-only view of the row's @ref stride_bytes bytes.
     */
    [[nodiscard]] std::span<const uint8_t> row(size_t y) const noexcept {
        return std::span<const uint8_t>(_data).subspan(y * stride_bytes(), stride_bytes());
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Uploads the full framebuffer to a panel's RAM.
     *
     * Places the framebuffer's origin at panel pixel (@p x, @p y). The
     * upload is invisible until the next @ref panel::refresh.
     *
     * @param panel The panel to upload to.
     * @param x     Destination column; must be a multiple of 8.
     * @param y     Destination row.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error.
     */
    void flush(panel& panel, size_t x = 0, size_t y = 0) const { unwrap(try_flush(panel, x, y)); }

    /**
     * @brief Uploads a horizontal band of the framebuffer to a panel's RAM.
     *
     * The band spans rows `[y_start, y_end)` across the full width and
     * lands at the same rows on the panel. The upload is invisible until
     * the next @ref panel::refresh.
     *
     * @param panel   The panel to upload to.
     * @param y_start First row of the band, inclusive.
     * @param y_end   End row of the band, exclusive; must satisfy `y_start < y_end <= height()`.
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled in menuconfig.
     * @throws std::system_error on error (e.g. an invalid row range).
     */
    void flush_rows(panel& panel, size_t y_start, size_t y_end) const { unwrap(try_flush_rows(panel, y_start, y_end)); }
#endif

    /**
     * @brief Uploads the full framebuffer to a panel's RAM.
     *
     * Places the framebuffer's origin at panel pixel (@p x, @p y). The
     * upload is invisible until the next @ref panel::try_refresh.
     *
     * @param panel The panel to upload to.
     * @param x     Destination column; must be a multiple of 8.
     * @param y     Destination row.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_flush(panel& panel, size_t x = 0, size_t y = 0) const {
        return panel.try_write(*this, x, y);
    }

    /**
     * @brief Uploads a horizontal band of the framebuffer to a panel's RAM.
     *
     * The band spans rows `[y_start, y_end)` across the full width and
     * lands at the same rows on the panel. The upload is invisible until
     * the next @ref panel::try_refresh.
     *
     * @param panel   The panel to upload to.
     * @param y_start First row of the band, inclusive.
     * @param y_end   End row of the band, exclusive; must satisfy `y_start < y_end <= height()`.
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_arg if the row range is invalid.
     */
    [[nodiscard]] result<void> try_flush_rows(panel& panel, size_t y_start, size_t y_end) const {
        return panel.try_write_rows(*this, y_start, y_end);
    }

private:
    mono_framebuffer(size_t width, size_t height, std::vector<uint8_t> data)
        : _width(width)
        , _height(height)
        , _data(std::move(data)) {}

    size_t _width;
    size_t _height;
    std::vector<uint8_t> _data;
};

} // namespace idfxx::epaper
