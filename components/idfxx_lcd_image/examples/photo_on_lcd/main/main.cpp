// SPDX-License-Identifier: Apache-2.0

#include "display_setup.hpp"

#include <idfxx/byte_source>
#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <idfxx/lcd/color>
#include <idfxx/lcd/image_writer>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstdint>
#include <span>
#include <system_error>

using namespace std::chrono_literals;

namespace image = idfxx::image;
namespace lcd = idfxx::lcd;

static constexpr idfxx::log::logger logger{"example"};

// The picture is written to the panel in bands of 16 rows. That is the default, and no frame buffer is needed either
// way, but it is named here because it bounds the bus's largest transfer, which the board setup below sizes for.
static constexpr lcd::write_options WRITE_OPTIONS{.band_rows = 16};

// The photo, embedded by main/CMakeLists.txt.
extern const uint8_t photo_jpg_start[] asm("_binary_photo_jpg_start");
extern const uint8_t photo_jpg_end[] asm("_binary_photo_jpg_end");

// 1. Shows the photo filling the whole screen.
static void show_full_screen(lcd::panel& panel, std::span<const uint8_t> jpeg) {
    idfxx::memory_source source{jpeg};
    idfxx::jpeg::decoder decoder(source);

    // By default the picture covers the screen, upright according to any EXIF orientation it has.
    lcd::write_image(panel, decoder, {}, WRITE_OPTIONS);
}

// 2. Shows the whole photo inside a rectangle, with a caption drawn over its bottom edge.
static void show_in_rectangle(lcd::panel& panel, std::span<const uint8_t> jpeg) {
    idfxx::memory_source source{jpeg};
    idfxx::jpeg::decoder decoder(source);

    // A frame inside the screen, with a margin of 40 pixels on each side and 10 above and below.
    const image::rect frame{
        .x = 40,
        .y = 10,
        .width = panel.display_width() - 2 * 40,
        .height = panel.display_height() - 2 * 10,
    };
    const image::placement where{
        .target = frame,
        .layout = image::layout::fit, // all of the photo, with black where it doesn't reach the frame
        .background = {0, 0, 0},
    };

    // The overlay is called with each band just before it is written. It draws in screen coordinates, and the
    // canvas clips to the band, so the caption appears in whichever band it falls in.
    lcd::write_image(panel, decoder, where, WRITE_OPTIONS, [&](lcd::rgb565_framebuffer& band, size_t x, size_t y) {
        idfxx::gfx::canvas canvas(band, x, y);
        canvas.fill_rect(frame.x, frame.y + frame.height - 20, frame.width, 20, {0, 0, 0});
        canvas.draw_text(idfxx::font::spleen_8x16, frame.x + 4, frame.y + frame.height - 18, "idfxx", {255, 255, 255});
    });
}

extern "C" void app_main() {
    try {
        display_setup::board board(WRITE_OPTIONS.band_rows);
        auto& panel = board.panel;
        panel.swap_xy(true);
        panel.display_on(true);
        logger.info("Display initialized ({}x{})", panel.display_width(), panel.display_height());

        const std::span<const uint8_t> jpeg{photo_jpg_start, photo_jpg_end};

        show_full_screen(panel, jpeg);
        logger.info("Full-screen photo shown");
        idfxx::delay(5s);

        show_in_rectangle(panel, jpeg);
        logger.info("Photo shown in a rectangle");

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
