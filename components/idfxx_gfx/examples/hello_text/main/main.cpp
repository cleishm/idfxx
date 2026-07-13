// SPDX-License-Identifier: Apache-2.0

// Draws on an SSD1306 OLED: a filled banner with inverse text, a big
// scaled headline, and a right-aligned status line.

#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/i2c/master>
#include <idfxx/lcd/mono_framebuffer>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/ssd1306>
#include <idfxx/log>
#include <idfxx/sched>

#include <chrono>

using namespace std::chrono_literals;
using namespace frequency_literals;

static constexpr idfxx::log::logger logger{"example"};

// Pin assignments — change to match your board.
static constexpr auto PIN_SDA = idfxx::gpio_8;
static constexpr auto PIN_SCL = idfxx::gpio_9;

extern "C" void app_main() {
    try {
        idfxx::i2c::master_bus bus(
            idfxx::i2c::port::i2c0,
            {
                .sda = PIN_SDA,
                .scl = PIN_SCL,
                .frequency = 400_kHz,
            }
        );
        idfxx::lcd::panel_io io(bus, idfxx::lcd::ssd1306::i2c_io_config());
        idfxx::lcd::ssd1306 display(io, {.height = 64});
        display.display_on(true);

        idfxx::lcd::mono_framebuffer fb(display.width(), display.height());
        idfxx::gfx::canvas canvas(fb);

        // Filled banner with inverse small text.
        canvas.fill_rect(0, 0, canvas.width(), 10, true);
        canvas.draw_text(idfxx::font::spleen_5x8, 2, 1, "idfxx_gfx demo", false);

        // Big headline: 8x16 at scale 2 = 16x32 glyphs.
        canvas.draw_text(idfxx::font::spleen_8x16, 4, 16, "23.7", true, 2);

        // Small status line, right-aligned.
        const auto status = "-72dBm 45s";
        const size_t w = idfxx::font::text_width(idfxx::font::spleen_5x8, status);
        canvas.draw_text(idfxx::font::spleen_5x8, canvas.width() - w - 1, 54, status);

        canvas.flush(display);
        logger.info("text drawn ({} px headline)", idfxx::font::text_width(idfxx::font::spleen_8x16, "23.7", 2));

        while (true) {
            idfxx::delay(1h);
        }
    } catch (const std::system_error& e) {
        logger.error("failed: {}", e.what());
    }
}
