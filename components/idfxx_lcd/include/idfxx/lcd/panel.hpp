// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/lcd/panel>
 * @file panel.hpp
 * @brief Abstract LCD panel interface.
 * @ingroup idfxx_lcd
 */

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

/**
 * @headerfile <idfxx/lcd/panel>
 * @brief Abstract base class for LCD panels.
 *
 * The public interface is non-virtual; concrete drivers (e.g.
 * `idfxx::lcd::ili9341`) customize behaviour by overriding the protected
 * `do_*` hooks, mirroring the standard library's non-virtual-interface
 * pattern (cf. `std::pmr::memory_resource`).
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

} // namespace idfxx::lcd
