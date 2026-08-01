// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/epaper/gray4_framebuffer>
 * @file gray4_framebuffer.hpp
 * @brief Framebuffer for 4-level grayscale ePaper displays.
 * @ingroup idfxx_epaper
 */

#include <idfxx/epaper/color>
#include <idfxx/epaper/panel>
#include <idfxx/error>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace idfxx::epaper {

/**
 * @headerfile <idfxx/epaper/gray4_framebuffer>
 * @brief In-memory framebuffer for 4-level grayscale ePaper displays.
 *
 * Each pixel is a @ref gray4 level (2 bits). The levels are stored as two
 * separate 1-bit-per-pixel planes — plane 0 holds bit 0 of each pixel's
 * level, plane 1 holds bit 1 — because that is how ePaper controllers
 * consume grayscale data: one plane per controller RAM bank. Within each
 * plane the layout matches @ref mono_framebuffer — row-major, MSB-first,
 * with rows padded to whole bytes (@ref stride_bytes).
 *
 * A new framebuffer starts all-white (`gray4::white`, level 0), so generic
 * drawing code that clears with a value-initialized pixel (e.g.
 * `idfxx::gfx::canvas::clear`) clears to blank paper.
 *
 * Draw into the framebuffer with @ref set_pixel and friends, then upload it
 * to a panel's RAM with @ref flush or @ref flush_rows, and make it visible
 * with @ref panel::refresh. This is a plain value type: copyable, movable,
 * and independent of any panel.
 *
 * @code
 * display.set_color_mode(idfxx::epaper::color_mode::gray4);
 * idfxx::epaper::gray4_framebuffer fb(display.width(), display.height());
 * fb.set_pixel(10, 20, idfxx::epaper::gray4::dark);
 * fb.flush(display);
 * display.refresh();
 * @endcode
 */
class gray4_framebuffer {
public:
    /** @brief The value type written by @ref set_pixel. */
    using pixel_type = gray4;

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
    [[nodiscard]] gray4_framebuffer(size_t width, size_t height)
        : gray4_framebuffer(unwrap(make(width, height))) {}
#endif

    /**
     * @brief Creates a framebuffer of the given dimensions, with all pixels white.
     *
     * @param width  Width in pixels; must be non-zero.
     * @param height Height in pixels; must be non-zero.
     *
     * @return The new gray4_framebuffer, or an error.
     * @retval idfxx::errc::invalid_arg if @p width or @p height is zero.
     */
    [[nodiscard]] static result<gray4_framebuffer> make(size_t width, size_t height) {
        if (width == 0 || height == 0) {
            return error(errc::invalid_arg);
        }
        // Both planes live in one allocation: plane 0 first, then plane 1.
        return gray4_framebuffer{width, height, std::vector<uint8_t>(2 * ((width + 7) / 8) * height)};
    }

    /** @brief Returns the width in pixels. */
    [[nodiscard]] size_t width() const noexcept { return _width; }

    /** @brief Returns the height in pixels. */
    [[nodiscard]] size_t height() const noexcept { return _height; }

    /**
     * @brief Returns the number of bytes per row within each plane.
     *
     * Rows are padded to whole bytes: `(width() + 7) / 8`. Padding bits sit
     * past the right edge and stay white.
     *
     * @return The row stride, in bytes.
     */
    [[nodiscard]] size_t stride_bytes() const noexcept { return (_width + 7) / 8; }

    /**
     * @brief Sets a single pixel to the given gray level.
     *
     * Out-of-range coordinates are ignored.
     *
     * @param x     Column, in `[0, width())`.
     * @param y     Row, in `[0, height())`.
     * @param level The gray level to write.
     */
    void set_pixel(size_t x, size_t y, gray4 level) noexcept {
        if (x >= _width || y >= _height) {
            return;
        }
        const size_t index = y * stride_bytes() + x / 8;
        const uint8_t mask = static_cast<uint8_t>(1u << (7 - x % 8));
        const auto bits = std::to_underlying(level);
        _set_plane_bit(_data[index], mask, (bits & 0x01) != 0);
        _set_plane_bit(_data[_plane_size() + index], mask, (bits & 0x02) != 0);
    }

    /**
     * @brief Returns the gray level of a single pixel.
     *
     * @param x Column, in `[0, width())`.
     * @param y Row, in `[0, height())`.
     * @return The pixel's gray level; `gray4::white` if the coordinates are
     *         out of range.
     */
    [[nodiscard]] gray4 get_pixel(size_t x, size_t y) const noexcept {
        if (x >= _width || y >= _height) {
            return gray4::white;
        }
        const size_t index = y * stride_bytes() + x / 8;
        const uint8_t mask = static_cast<uint8_t>(1u << (7 - x % 8));
        uint8_t bits = 0;
        if ((_data[index] & mask) != 0) {
            bits |= 0x01;
        }
        if ((_data[_plane_size() + index] & mask) != 0) {
            bits |= 0x02;
        }
        return static_cast<gray4>(bits);
    }

    /**
     * @brief Sets every pixel to the given gray level.
     * @param level The gray level to fill with.
     */
    void fill(gray4 level) noexcept {
        const auto bits = std::to_underlying(level);
        auto plane0 = std::span<uint8_t>(_data).first(_plane_size());
        auto plane1 = std::span<uint8_t>(_data).last(_plane_size());
        std::ranges::fill(plane0, (bits & 0x01) != 0 ? uint8_t{0xFF} : uint8_t{0x00});
        std::ranges::fill(plane1, (bits & 0x02) != 0 ? uint8_t{0xFF} : uint8_t{0x00});
    }

    /** @brief Blanks every pixel to white (equivalent to `fill(gray4::white)`). */
    void clear() noexcept { fill(gray4::white); }

    /**
     * @brief Returns the raw pixel data of one plane.
     *
     * Each plane holds `stride_bytes() * height()` bytes in the row-major
     * MSB-first layout described in the class documentation. Plane 0 holds
     * bit 0 of each pixel's gray level, plane 1 holds bit 1.
     *
     * Unlike @ref set_pixel / @ref get_pixel, the raw views do not tolerate
     * out-of-range arguments: passing one is undefined behavior.
     *
     * @param index The plane to view: 0 or 1.
     * @return A read-only view of the plane's pixel data.
     */
    [[nodiscard]] std::span<const uint8_t> plane(size_t index) const noexcept {
        return std::span<const uint8_t>(_data).subspan(index * _plane_size(), _plane_size());
    }

    /**
     * @brief Returns the raw bytes of a single row of one plane.
     *
     * Unlike @ref set_pixel / @ref get_pixel, the raw views do not tolerate
     * out-of-range arguments: passing one is undefined behavior.
     *
     * @param index The plane to view: 0 or 1.
     * @param y     Row, in `[0, height())`.
     * @return A read-only view of the row's @ref stride_bytes bytes.
     */
    [[nodiscard]] std::span<const uint8_t> plane_row(size_t index, size_t y) const noexcept {
        return plane(index).subspan(y * stride_bytes(), stride_bytes());
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Uploads the full framebuffer to a panel's RAM.
     *
     * Places the framebuffer's origin at panel pixel (@p x, @p y). The
     * upload is invisible until the next @ref panel::refresh. The panel
     * must be operating in @ref color_mode::gray4.
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
     * the next @ref panel::refresh. The panel must be operating in
     * @ref color_mode::gray4.
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
     * upload is invisible until the next @ref panel::try_refresh. The panel
     * must be operating in @ref color_mode::gray4.
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
     * the next @ref panel::try_refresh. The panel must be operating in
     * @ref color_mode::gray4.
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
    gray4_framebuffer(size_t width, size_t height, std::vector<uint8_t> data)
        : _width(width)
        , _height(height)
        , _data(std::move(data)) {}

    [[nodiscard]] size_t _plane_size() const noexcept { return stride_bytes() * _height; }

    static void _set_plane_bit(uint8_t& byte, uint8_t mask, bool on) noexcept {
        if (on) {
            byte |= mask;
        } else {
            byte &= static_cast<uint8_t>(~mask);
        }
    }

    size_t _width;
    size_t _height;
    std::vector<uint8_t> _data; // plane 0 followed by plane 1
};

} // namespace idfxx::epaper
