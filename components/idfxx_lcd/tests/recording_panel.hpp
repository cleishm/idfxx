// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Shared test fixture for idfxx_lcd framebuffer tests.

#pragma once

#include "idfxx/lcd/panel"

#include <vector>

namespace idfxx_lcd_test {

// Stub panel recording draw_bitmap calls, for verifying flush behaviour
// without hardware.
class recording_panel : public idfxx::lcd::panel {
public:
    struct draw {
        int x_start;
        int y_start;
        int x_end;
        int y_end;
        const void* data;
    };

    std::vector<draw> draws;

private:
    [[nodiscard]] esp_lcd_panel_handle_t do_idf_handle() const override { return nullptr; }

    [[nodiscard]] idfxx::result<void>
    do_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void* color_data) override {
        draws.push_back({x_start, y_start, x_end, y_end, color_data});
        return {};
    }
};

} // namespace idfxx_lcd_test
