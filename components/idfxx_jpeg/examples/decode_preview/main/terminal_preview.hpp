// SPDX-License-Identifier: Apache-2.0

// Only prints an image to the terminal; you don't need to read this to follow the example.

#pragma once

#include <idfxx/image>

#include <cstdio>
#include <format>
#include <string>

namespace terminal_preview {

// Prints an image as 24-bit color text, two rows of pixels to a line.
inline void print_image(idfxx::image::view<const idfxx::image::rgb888> picture) {
    for (size_t y = 0; y < picture.height(); y += 2) {
        std::string line;
        for (size_t x = 0; x < picture.width(); ++x) {
            const idfxx::image::rgb888 upper = picture[x, y];
            const idfxx::image::rgb888 lower = y + 1 < picture.height() ? picture[x, y + 1] : idfxx::image::rgb888{};
            // An upper half block (U+2580): the foreground color fills its top half, the background its bottom.
            line +=
                std::format("\x1b[38;2;{};{};{};48;2;{};{};{}m▀", upper.r, upper.g, upper.b, lower.r, lower.g, lower.b);
        }
        line += "\x1b[0m\n";
        fputs(line.c_str(), stdout);
    }
}

} // namespace terminal_preview
