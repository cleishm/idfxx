// SPDX-License-Identifier: Apache-2.0

#include <idfxx/i2c/master>
#include <idfxx/lcd/mono_framebuffer>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/ssd1306>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>
#include <cstddef>

using namespace std::chrono_literals;
using namespace frequency_literals;

static constexpr idfxx::log::logger logger{"example"};

// Pin assignments — change to match your board.
static constexpr auto PIN_SDA = idfxx::gpio_8;
static constexpr auto PIN_SCL = idfxx::gpio_9;

extern "C" void app_main() {
    try {
        // --- I2C bus ---
        idfxx::i2c::master_bus bus(
            idfxx::i2c::port::i2c0,
            {
                .sda = PIN_SDA,
                .scl = PIN_SCL,
                .frequency = 400_kHz,
            }
        );

        // --- Panel I/O (i2c_io_config supplies the SSD1306 framing; some
        // modules use address 0x3D) ---
        idfxx::lcd::panel_io io(bus, idfxx::lcd::ssd1306::i2c_io_config());

        // --- SSD1306 panel ---
        idfxx::lcd::ssd1306 display(io, {.height = 64}); // use 32 for 128x32 modules

        display.display_on(true);
        logger.info("Display initialized ({}x{})", display.width(), display.height());

        // --- Draw a test pattern into a monochrome framebuffer ---
        idfxx::lcd::mono_framebuffer fb(display.width(), display.height());

        // Border
        for (size_t x = 0; x < fb.width(); ++x) {
            fb.set_pixel(x, 0, true);
            fb.set_pixel(x, fb.height() - 1, true);
        }
        for (size_t y = 0; y < fb.height(); ++y) {
            fb.set_pixel(0, y, true);
            fb.set_pixel(fb.width() - 1, y, true);
        }

        // Both diagonals, forming an X across the full display
        for (size_t y = 0; y < fb.height(); ++y) {
            const size_t x = y * fb.width() / fb.height();
            fb.set_pixel(x, y, true);
            fb.set_pixel(fb.width() - 1 - x, y, true);
        }

        // Checkerboard band along the bottom edge
        for (size_t y = fb.height() - 16; y < fb.height(); ++y) {
            for (size_t x = 0; x < fb.width(); ++x) {
                fb.set_pixel(x, y, ((x / 4 + y / 4) % 2) == 0);
            }
        }

        fb.flush(display);
        logger.info("Drew test pattern");

        // --- Animate: sweep a progress bar with partial updates, then invert ---
        bool inverted = false;
        size_t bar_x = 2;
        while (true) {
            idfxx::delay(25ms);

            for (size_t y = 26; y < 30; ++y) {
                fb.set_pixel(bar_x, y, true);
            }
            // Only the affected column is transferred (rows rounded out to the
            // containing page, i.e. rows 24-31): a single byte on the bus.
            fb.flush_region(display, bar_x, 26, bar_x + 1, 30);

            if (++bar_x < fb.width() - 2) {
                continue;
            }

            // Bar complete: clear it (restoring the diagonals), toggle
            // inversion, and start again.
            for (size_t y = 26; y < 30; ++y) {
                for (size_t x = 2; x < fb.width() - 2; ++x) {
                    fb.set_pixel(x, y, false);
                }
                const size_t dx = y * fb.width() / fb.height();
                fb.set_pixel(dx, y, true);
                fb.set_pixel(fb.width() - 1 - dx, y, true);
            }
            fb.flush_rows(display, 26, 30);
            inverted = !inverted;
            display.invert_color(inverted);
            bar_x = 2;
        }

    } catch (const std::system_error& e) {
        logger.error("OLED error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
