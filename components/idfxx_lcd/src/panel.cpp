// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/lcd/panel>

#include <esp_lcd_panel_ops.h>

namespace idfxx::lcd {

result<void> panel::do_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data) {
    auto handle = do_idf_handle();
    if (handle == nullptr) {
        return error(errc::invalid_state);
    }
    return wrap(esp_lcd_panel_draw_bitmap(handle, x_start, y_start, x_end, y_end, color_data));
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
