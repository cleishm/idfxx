// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/mono_framebuffer>
#include <idfxx/lcd/panel>
#include <idfxx/lcd/rgb565_framebuffer>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <span>

namespace idfxx::lcd {

result<void> panel::try_write(const rgb565_framebuffer& fb, size_t x, size_t y) {
    return try_write_region(fb, 0, 0, fb.width(), fb.height(), x, y);
}

result<void> panel::try_write(const mono_framebuffer& fb, size_t x, size_t y) {
    return try_write_region(fb, 0, 0, fb.width(), fb.height(), x, y);
}

result<void> panel::try_write_rows(
    const rgb565_framebuffer& fb,
    size_t row_start,
    size_t row_end,
    size_t x,
    size_t y
) {
    return try_write_region(fb, 0, row_start, fb.width(), row_end, x, y);
}

result<void> panel::try_write_rows(
    const mono_framebuffer& fb,
    size_t row_start,
    size_t row_end,
    size_t x,
    size_t y
) {
    return try_write_region(fb, 0, row_start, fb.width(), row_end, x, y);
}

result<void> panel::try_write_region(
    const rgb565_framebuffer& fb,
    size_t x_start,
    size_t y_start,
    size_t x_end,
    size_t y_end,
    size_t x,
    size_t y
) {
    const size_t width = fb.width();
    if (x_start >= x_end || x_end > width || y_start >= y_end || y_end > fb.height()) {
        return error(errc::invalid_arg);
    }
    const auto data = fb.data();
    if (x_start == 0 && x_end == width) {
        // In the row-major layout a full-width band is contiguous, so a
        // single transfer covers all its rows.
        return try_draw_bitmap(
            static_cast<int>(x),
            static_cast<int>(y + y_start),
            static_cast<int>(x + width),
            static_cast<int>(y + y_end),
            std::as_bytes(data.subspan(y_start * width, (y_end - y_start) * width))
        );
    }
    for (size_t row = y_start; row < y_end; ++row) {
        auto drawn = try_draw_bitmap(
            static_cast<int>(x + x_start),
            static_cast<int>(y + row),
            static_cast<int>(x + x_end),
            static_cast<int>(y + row + 1),
            std::as_bytes(data.subspan(row * width + x_start, x_end - x_start))
        );
        if (!drawn) {
            return drawn;
        }
    }
    return {};
}

result<void> panel::try_write_region(
    const mono_framebuffer& fb,
    size_t x_start,
    size_t y_start,
    size_t x_end,
    size_t y_end,
    size_t x,
    size_t y
) {
    const size_t width = fb.width();
    if (x_start >= x_end || x_end > width || y_start >= y_end || y_end > fb.height() ||
        y % 8 != 0) {
        return error(errc::invalid_arg);
    }
    if (_bits_per_pixel != 1) {
        return error(errc::invalid_size);
    }
    if (_swapped) {
        // The framebuffer's pages group rows; a swapped panel pages along columns.
        return error(errc::invalid_state);
    }
    const auto data = fb.data();
    const size_t first_page = y_start / 8;
    const size_t end_page = (y_end + 7) / 8;
    if (x_start == 0 && x_end == width) {
        // In the page-major layout a full-width band is contiguous, so a
        // single transfer covers all its pages.
        return try_draw_bitmap(
            static_cast<int>(x),
            static_cast<int>(y + first_page * 8),
            static_cast<int>(x + width),
            static_cast<int>(y + end_page * 8),
            data.subspan(first_page * width, (end_page - first_page) * width)
        );
    }
    for (size_t page = first_page; page < end_page; ++page) {
        auto drawn = try_draw_bitmap(
            static_cast<int>(x + x_start),
            static_cast<int>(y + page * 8),
            static_cast<int>(x + x_end),
            static_cast<int>(y + (page + 1) * 8),
            data.subspan(page * width + x_start, x_end - x_start)
        );
        if (!drawn) {
            return drawn;
        }
    }
    return {};
}

result<void>
panel::try_draw_bitmap(int x_start, int y_start, int x_end, int y_end, byte_view color_data) {
    if (x_start < 0 || y_start < 0 || x_start >= x_end || y_start >= y_end) {
        return error(errc::invalid_arg);
    }
    const auto columns = static_cast<size_t>(x_end - x_start);
    const auto rows = static_cast<size_t>(y_end - y_start);
    // A region too large to compute the size of has more data than any view can hold.
    size_t pixels = 0;
    size_t bits = 0;
    if (__builtin_mul_overflow(columns, rows, &pixels) ||
        __builtin_mul_overflow(pixels, _bits_per_pixel, &bits) || color_data.size() != bits / 8) {
        return error(errc::invalid_size);
    }
    return do_draw_bitmap(x_start, y_start, x_end, y_end, color_data);
}

result<void>
panel::do_draw_bitmap(int x_start, int y_start, int x_end, int y_end, byte_view color_data) {
    auto handle = do_idf_handle();
    if (handle == nullptr) {
        return error(errc::invalid_state);
    }
    auto drawn =
        wrap(esp_lcd_panel_draw_bitmap(handle, x_start, y_start, x_end, y_end, color_data.data()));
    if (_queued_io == nullptr) {
        return drawn;
    }
    // The panel I/O may still be reading color_data in the background. A
    // parameterless tx_param waits for every queued transfer and sends
    // nothing; wait even after a failed draw, as earlier chunks may be queued.
    auto waited = wrap(esp_lcd_panel_io_tx_param(_queued_io, -1, nullptr, 0));
    return drawn ? waited : drawn;
}

result<void> panel::do_invert_color(bool invert) {
    auto handle = do_idf_handle();
    if (handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_invert_color(handle, invert));
}

result<void> panel::do_swap_xy(bool swap) {
    auto handle = do_idf_handle();
    if (handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_swap_xy(handle, swap));
}

result<void> panel::do_mirror(bool mirror_x, bool mirror_y) {
    auto handle = do_idf_handle();
    if (handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_mirror(handle, mirror_x, mirror_y));
}

result<void> panel::do_display_on(bool on) {
    auto handle = do_idf_handle();
    if (handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_disp_on_off(handle, on));
}

} // namespace idfxx::lcd
