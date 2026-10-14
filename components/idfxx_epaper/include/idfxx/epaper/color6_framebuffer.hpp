// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/color6_framebuffer>
 * @file color6_framebuffer.hpp
 * @brief Framebuffer for six-color (E Ink Spectra 6) ePaper displays.
 * @ingroup idfxx_epaper
 */

#include <idfxx/epaper/color>
#include <idfxx/error>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

/**
 * @headerfile <idfxx/epaper/color6_framebuffer>
 * @brief ePaper display driver classes.
 */
namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/color6_framebuffer>
 * @brief In-memory framebuffer for six-color (E Ink Spectra 6) ePaper displays.
 *
 * Each pixel is a @ref color6 value (4 bits). Pixels are stored row-major,
 * two per byte, in the native format of Spectra 6 controllers: the **high
 * nibble holds the left (even-x) pixel** and the low nibble the right
 * (odd-x) one, rows are laid out top-to-bottom, and each row is padded to a
 * whole number of bytes (@ref stride_bytes). The byte for pixel (x, y) is at
 * index `y * stride_bytes() + x / 2`. The nibble values are the panel-native
 * codes, so the bytes stream to the controller unchanged.
 *
 * A new framebuffer starts all-white (`color6::white`). Note that a
 * value-initialized `color6{}` is black, so a *windowed*
 * `idfxx::gfx::canvas::clear` inks its region rather than blanking it;
 * whole-surface clears use this class's own @ref clear and blank to paper.
 *
 * Draw into the framebuffer with @ref set_pixel and friends, then upload it
 * to a panel's RAM with @ref panel::write (full frame) or
 * @ref panel::write_rows (a horizontal band), and make it visible with
 * @ref panel::refresh. This is a plain value type: copyable, movable, and
 * independent of any panel. Its storage is a plain `std::vector`: a full
 * 1200x1600 frame is 960 KB, which lands in PSRAM automatically when
 * `CONFIG_SPIRAM_USE_MALLOC` is enabled (large allocations go external by
 * default); without PSRAM, draw through a band-sized framebuffer with
 * `idfxx::gfx::render_banded` instead.
 *
 * @code
 * display.set_color_mode(idfxx::epaper::color_mode::color6);
 * idfxx::epaper::color6_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, idfxx::epaper::color6::red);
 * display.write(fb);
 * display.refresh();
 * @endcode
 */
class color6_framebuffer {
public:
    /** @brief The value type written by @ref set_pixel. */
    using pixel_type = color6;

    /**
     * @brief Destination-column alignment required by @ref panel::write.
     * Two pixels share a byte, so a framebuffer must land on an even column.
     */
    static constexpr size_t x_alignment = 2;

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
    [[nodiscard]] color6_framebuffer(size_t width, size_t height)
        : color6_framebuffer(unwrap(make(width, height))) {}
#endif

    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels white.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero.
     *
     * @return The new color6_framebuffer, or an error.
     * @retval idfxx::errc::invalid_arg if @p width or @p height is zero.
     */
    [[nodiscard]] static result<color6_framebuffer> make(size_t width, size_t height) {
        if (width == 0 || height == 0) {
            return error(errc::invalid_arg);
        }
        return color6_framebuffer{
            width, height, std::vector<uint8_t>(((width + 1) / 2) * height, _pair(color6::white))
        };
    }

    /** @brief Returns the width in pixels. */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /** @brief Returns the height in pixels. */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Returns the number of bytes per row.
     *
     * Rows are padded to whole bytes: `(width() + 1) / 2`. On an odd-width
     * framebuffer the padding nibble sits past the right edge and stays
     * white.
     *
     * @return The row stride, in bytes.
     */
    [[nodiscard]] size_t stride_bytes() const noexcept { return (_width + 1) / 2; }

    /**
     * @brief Sets a single pixel to the given color.
     *
     * Out-of-range coordinates are ignored.
     *
     * @param x     Column, in `[0, width())`.
     * @param y     Row, in `[0, height())`.
     * @param color The color to write.
     */
    void set_pixel(size_t x, size_t y, color6 color) noexcept {
        if (x >= _width || y >= _height) {
            return;
        }
        uint8_t& byte = _data[y * stride_bytes() + x / 2];
        const auto code = static_cast<uint8_t>(std::to_underlying(color) & 0x0F);
        if (x % 2 == 0) {
            byte = static_cast<uint8_t>((byte & 0x0F) | (code << 4));
        } else {
            byte = static_cast<uint8_t>((byte & 0xF0) | code);
        }
    }

    /**
     * @brief Returns the color of a single pixel.
     *
     * @param x Column, in `[0, width())`.
     * @param y Row, in `[0, height())`.
     * @return The pixel's color, or `color6::white` if the coordinates are
     *         out of range.
     */
    [[nodiscard]] color6 get_pixel(size_t x, size_t y) const noexcept {
        if (x >= _width || y >= _height) {
            return color6::white;
        }
        const uint8_t byte = _data[y * stride_bytes() + x / 2];
        return static_cast<color6>(x % 2 == 0 ? byte >> 4 : byte & 0x0F);
    }

    /**
     * @brief Sets every pixel to the given color.
     * @param color The color to fill with.
     */
    void fill(color6 color) noexcept {
        std::ranges::fill(_data, _pair(color));
        // Keep the row padding nibble white.
        if (_width % 2 != 0) {
            const auto padded = static_cast<uint8_t>((_pair(color) & 0xF0) | (_pair(color6::white) & 0x0F));
            const size_t stride = stride_bytes();
            for (size_t y = 0; y < _height; ++y) {
                _data[y * stride + stride - 1] = padded;
            }
        }
    }

    /** @brief Blanks every pixel to white (equivalent to `fill(color6::white)`). */
    void clear() noexcept { fill(color6::white); }

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

private:
    color6_framebuffer(size_t width, size_t height, std::vector<uint8_t> data)
        : _width(width)
        , _height(height)
        , _data(std::move(data)) {}

    // The byte holding two horizontally adjacent pixels of the same color.
    [[nodiscard]] static constexpr uint8_t _pair(color6 color) noexcept {
        const auto code = static_cast<uint8_t>(std::to_underlying(color) & 0x0F);
        return static_cast<uint8_t>((code << 4) | code);
    }

    size_t _width;
    size_t _height;
    std::vector<uint8_t> _data;
};

} // namespace idfxx::epaper
