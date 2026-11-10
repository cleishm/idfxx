// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Shared test fixture for idfxx_lcd framebuffer tests.

#pragma once

#include "idfxx/lcd/panel"

#include <vector>

namespace idfxx_lcd_test {

// Stub panel recording draw_bitmap calls, for verifying framebuffer write
// behavior without hardware.
class recording_panel : public idfxx::lcd::panel {
public:
    recording_panel() = default;
    recording_panel(size_t width, size_t height)
        : idfxx::lcd::panel(width, height) {}

    struct draw {
        int x_start;
        int y_start;
        int x_end;
        int y_end;
        const void* data;
    };

    std::vector<draw> draws;
    // When set, swap_xy fails instead of succeeding.
    bool fail_swap_xy = false;

private:
    [[nodiscard]] esp_lcd_panel_handle_t do_idf_handle() const override { return nullptr; }

    [[nodiscard]] idfxx::result<void>
    do_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data) override {
        draws.push_back({x_start, y_start, x_end, y_end, color_data});
        return {};
    }

    [[nodiscard]] idfxx::result<void> do_swap_xy(bool) override {
        if (fail_swap_xy) {
            return idfxx::error(idfxx::errc::fail);
        }
        return {};
    }
};

} // namespace idfxx_lcd_test
