// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/font.hpp>

namespace idfxx::font {

void draw_text(
    lcd::mono_framebuffer& fb,
    const mono_font& font,
    size_t x,
    size_t y,
    std::string_view text,
    bool on,
    unsigned scale
) noexcept {
    if (scale == 0) {
        scale = 1;
    }
    const size_t bpr = font.bytes_per_row();
    size_t cell_x = x;

    for (char c : text) {
        if (font.contains(c)) {
            const uint8_t* glyph = font.glyph(c);
            for (size_t row = 0; row < font.height; ++row) {
                for (size_t col = 0; col < font.width; ++col) {
                    const uint8_t byte = glyph[row * bpr + col / 8];
                    if (!(byte & (0x80u >> (col % 8)))) {
                        continue;
                    }
                    const size_t px = cell_x + col * scale;
                    const size_t py = y + row * scale;
                    for (unsigned sy = 0; sy < scale; ++sy) {
                        for (unsigned sx = 0; sx < scale; ++sx) {
                            fb.set_pixel(px + sx, py + sy, on);
                        }
                    }
                }
            }
        }
        cell_x += font.width * scale;
    }
}

} // namespace idfxx::font
