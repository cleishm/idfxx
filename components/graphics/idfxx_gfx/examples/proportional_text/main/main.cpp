// SPDX-License-Identifier: Apache-2.0

// Draws a small café menu on an ILI9341 color LCD in the Inter typeface:
// a centered heading and subtitle, left-aligned dishes stacked line by
// line, a paragraph wrapped to the screen width, and a right-aligned
// footer. Proportional fonts take UTF-8 text, so accents and curly quotes
// are written as-is in the string literals.
//
// The whole 240x320 frame is authored once in screen coordinates in
// draw_frame(); render_banded runs it once per 240x40 band and writes each
// slice, so the framebuffer stays small (19 KB) instead of a full 150 KB
// frame.

#include <idfxx/font/inter>
#include <idfxx/gfx>
#include <idfxx/gpio>
#include <idfxx/lcd/ili9341>
#include <idfxx/lcd/rgb565_framebuffer>
#include <idfxx/log>
#include <idfxx/panel_io>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <array>
#include <chrono>
#include <string_view>

using namespace std::chrono_literals;
using namespace quanta::frequency_literals;

static constexpr idfxx::log::logger logger{"example"};

// Pin assignments — change to match your board.
static constexpr auto PIN_MOSI = idfxx::gpio_11;
static constexpr auto PIN_SCLK = idfxx::gpio_12;
static constexpr auto PIN_CS = idfxx::gpio_10;
static constexpr auto PIN_DC = idfxx::gpio_9;
static constexpr auto PIN_RST = idfxx::gpio_14;
static constexpr auto PIN_BL = idfxx::gpio_15;

static constexpr size_t DISPLAY_W = 240;
static constexpr size_t DISPLAY_H = 320;
static constexpr size_t BAND_H = 40;

// Left and right margin; text is laid out in the box between them.
static constexpr size_t MARGIN = 8;
static constexpr size_t TEXT_W = DISPLAY_W - 2 * MARGIN;

using idfxx::gfx::text_align;
using idfxx::lcd::rgb565;
using band_canvas = idfxx::gfx::canvas<idfxx::lcd::rgb565_framebuffer>;

struct dish {
    std::string_view name;
    std::string_view description;
};
static constexpr std::array<dish, 2> DISHES{{
    {"Crème brûlée", "The “naïve” classic, à la carte"},
    {"Café au lait", "Espresso with steamed milk"},
}};

// Draws the whole frame in 240x320 screen coordinates; render_banded
// invokes this once per band and clips everything outside the band.
static void draw_frame(band_canvas& canvas) {
    constexpr rgb565 amber(255, 191, 0);
    constexpr rgb565 white(255, 255, 255);
    constexpr rgb565 gray(160, 160, 160);

    const auto& heading = idfxx::font::inter_48;
    const auto& subtitle = idfxx::font::inter_24;
    const auto& name = idfxx::font::inter_32;
    const auto& body = idfxx::font::inter_16;

    // Text is placed by the top-left corner of its line, so each line
    // starts one line_height below the previous one. draw_text_box returns
    // the height it drew, which moves y past it.
    int y = 4;

    // Heading and subtitle, centered in the text box.
    y += canvas.draw_text_box(heading, MARGIN, y, TEXT_W, "Café", amber, text_align::center);
    y += canvas.draw_text_box(
        subtitle, MARGIN, y, TEXT_W, "Menu du jour", white, text_align::center
    );
    y += 4;
    canvas.draw_hline(MARGIN, y, TEXT_W, gray);
    y += 8;

    // Dishes, left-aligned: a name in the 32 px font, then a description
    // in the 16 px font.
    for (const dish& d : DISHES) {
        canvas.draw_text(name, MARGIN, y, d.name, white);
        y += name.line_height;
        canvas.draw_text(body, MARGIN, y, d.description, gray);
        y += body.line_height + 12;
    }

    // A paragraph wrapped to the text box: lines break between words, and
    // the soft hyphen (U+00AD) marks where a long word may be hyphenated.
    canvas.draw_text_box(
        body, MARGIN, y, TEXT_W, "Baked fresh daily with ingre\u00ADdients from local farms.", white
    );

    // Footer, right-aligned against the bottom of the screen.
    canvas.draw_text_box(
        body,
        MARGIN,
        DISPLAY_H - body.line_height - 2,
        TEXT_W,
        "Served daily, 8–11 am",
        amber,
        text_align::right
    );
}

extern "C" void app_main() {
    try {
        // --- Backlight on ---
        idfxx::gpio backlight = PIN_BL;
        idfxx::configure_gpios({.mode = idfxx::gpio::mode::output}, backlight);
        backlight.set_level(idfxx::gpio::level::high);

        // --- SPI bus ---
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.sclk = PIN_SCLK;
        bus_cfg.max_transfer_sz = DISPLAY_W * BAND_H * sizeof(rgb565);
        idfxx::spi::master_bus bus(
            idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg
        );

        // --- Panel I/O ---
        idfxx::panel_io io(
            bus,
            {
                .cs_gpio = PIN_CS,
                .dc_gpio = PIN_DC,
                .spi_mode = 0,
                .pclk_freq = 40_MHz,
                .trans_queue_depth = 10,
                .lcd_cmd_bits = 8,
                .lcd_param_bits = 8,
            }
        );

        // --- ILI9341 panel ---
        idfxx::lcd::ili9341 panel(
            io,
            {
                .reset_gpio = PIN_RST,
                .rgb_element_order = idfxx::lcd::rgb_element_order::bgr,
                .bits_per_pixel = 16,
            }
        );

        panel.display_on(true);
        logger.info("Display initialized ({}x{})", DISPLAY_W, DISPLAY_H);

        idfxx::lcd::rgb565_framebuffer band(DISPLAY_W, BAND_H);
        idfxx::gfx::render_banded(band, panel, DISPLAY_H, [](band_canvas& canvas) {
            draw_frame(canvas);
        });

        logger.info("Drew the menu");

    } catch (const std::system_error& e) {
        logger.error("LCD error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
