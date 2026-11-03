// SPDX-License-Identifier: Apache-2.0

#include "display_setup.hpp"

#include <idfxx/byte_source>
#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/image_writer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/uc8179>
#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstdint>
#include <span>
#include <system_error>

using namespace std::chrono_literals;

namespace image = idfxx::image;
namespace epaper = idfxx::epaper;

static constexpr idfxx::log::logger logger{"example"};

// The photo, embedded by main/CMakeLists.txt.
extern const uint8_t photo_jpg_start[] asm("_binary_photo_jpg_start");
extern const uint8_t photo_jpg_end[] asm("_binary_photo_jpg_end");

// 1. Starts the gray levels from a known state: black, then white, whatever was shown before.
static void prepare_panel(epaper::uc8179& panel) {
    epaper::mono_framebuffer black(panel.width(), panel.height());
    black.fill(true);
    panel.write(black);
    panel.refresh();
    panel.clear();
    panel.refresh();
    panel.set_color_mode(epaper::color_mode::gray4);
}

// 2. Writes the photo filling the whole screen.
static void show_full_screen(epaper::panel& panel, std::span<const uint8_t> jpeg) {
    idfxx::memory_source source{jpeg};
    idfxx::jpeg::decoder decoder(source);

    // By default the picture covers the screen, upright according to any EXIF orientation it has.
    epaper::write_image(panel, epaper::calibration::gdey075t7_gray4(), decoder);
}

// 3. Writes the whole photo inside a rectangle over the first one, with a caption drawn over its bottom edge.
static void show_in_rectangle(epaper::panel& panel, std::span<const uint8_t> jpeg) {
    idfxx::memory_source source{jpeg};
    idfxx::jpeg::decoder decoder(source);

    // A rectangle's left and right edges must be multiples of 8 on this panel.
    const image::rect frame{.x = 400, .y = 120, .width = 360, .height = 240};
    const image::placement where{
        .target = frame,
        .layout = image::layout::fit, // all of the photo, with white where it doesn't reach the frame
    };

    // The overlay is called with each band just before it is written. It draws in screen coordinates, and the
    // canvas clips to the band, so the caption appears in whichever bands it falls in.
    epaper::write_image<epaper::gray4_framebuffer>(
        panel,
        epaper::calibration::gdey075t7_gray4(),
        decoder,
        where,
        [&](epaper::gray4_framebuffer& band, size_t x, size_t y) {
            idfxx::gfx::canvas canvas(band, x, y);
            canvas.fill_rect(frame.x, frame.y + frame.height - 20, frame.width, 20, epaper::gray4::white);
            canvas.draw_text(
                idfxx::font::spleen_8x16, frame.x + 4, frame.y + frame.height - 18, "idfxx", epaper::gray4::black
            );
        }
    );
}

extern "C" void app_main() {
    try {
        display_setup::board board;
        auto& display = board.panel;
        logger.info("Display initialized ({}x{})", display.width(), display.height());

        const std::span<const uint8_t> jpeg{photo_jpg_start, photo_jpg_end};

        prepare_panel(display);

        logger.info("Converting the photo twice");
        show_full_screen(display, jpeg);
        show_in_rectangle(display, jpeg);

        logger.info("Refreshing");
        display.refresh();
        display.sleep();
        logger.info("Photo shown; it stays on the panel without power");

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
