// SPDX-License-Identifier: Apache-2.0

#include "display_setup.hpp"

#include <idfxx/byte_source>
#include <idfxx/epaper/ink_writer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/uc8179>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstdint>
#include <span>
#include <system_error>

using namespace std::chrono_literals;

namespace epaper = idfxx::epaper;

static constexpr idfxx::log::logger logger{"example"};

// The frame, embedded by main/CMakeLists.txt: 800x480 pixels of 2-bit inks, 96,000 bytes.
extern const uint8_t frame_bin_start[] asm("_binary_frame_bin_start");
extern const uint8_t frame_bin_end[] asm("_binary_frame_bin_end");

// Starts the gray levels from a known state: black, then white, whatever was shown before.
static void prepare_panel(epaper::uc8179& panel) {
    epaper::mono_framebuffer black(panel.width(), panel.height());
    black.fill(true);
    panel.write(black);
    panel.refresh();
    panel.clear();
    panel.refresh();
    panel.set_color_mode(epaper::color_mode::gray4);
}

extern "C" void app_main() {
    try {
        display_setup::board board;
        auto& display = board.panel;
        logger.info("Display initialized ({}x{})", display.width(), display.height());

        // A real device would read the frame from a connection, through a byte_source type written
        // for it. Any byte_source works here, so this is the only line of this code that changes.
        idfxx::memory_source source{std::span<const uint8_t>{frame_bin_start, frame_bin_end}};

        // The frame is 2 bits per pixel, so the panel must be in four grays before it is written.
        prepare_panel(display);
        epaper::write_inks(display, source);

        logger.info("Refreshing");
        display.refresh();
        display.sleep();
        logger.info("Frame shown; it stays on the panel without power");

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
