// SPDX-License-Identifier: Apache-2.0

#include "display_setup.hpp"

#include <idfxx/byte_source>
#include <idfxx/epaper/image_writer>
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

// Turns the photo a quarter turn counter-clockwise on the panel's native portrait layout, so it
// shows upright with the E1004 in landscape. This one is stored on its side, so the turn cancels
// its EXIF orientation: it is written exactly as stored, row by row, with no frame in memory. A
// photo stored upright would take a 5.76 MB frame.
static constexpr image::orientation TURN = image::orientation::rotate_270;

extern "C" void app_main() {
    try {
        display_setup::board board;
        auto& display = board.panel;
        display.set_color_mode(epaper::color_mode::color6);
        logger.info("Display initialized ({}x{})", display.width(), display.height());

        // --- Photo ---
        idfxx::memory_source source{std::span<const uint8_t>{photo_jpg_start, photo_jpg_end}};
        idfxx::jpeg::decoder decoder(source);

        // By default the picture covers the screen.
        logger.info("Converting the photo, which takes over a minute");
        epaper::write_image(
            display, epaper::calibration::el133uf1(), decoder, {.orientation = TURN}
        );

        logger.info("Refreshing, which takes about 30 seconds");
        display.refresh();
        display.sleep();
        logger.info("Full-screen photo shown; it stays on the panel without power");

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
