// SPDX-License-Identifier: Apache-2.0

// Draws on an ILI9341 color LCD: a header with scaled text, labelled
// color swatches, and a right-aligned footer. The whole 240x320 frame is
// authored once in screen coordinates in draw_frame(); render_banded runs
// it once per 240x40 band and flushes each slice, so the framebuffer
// stays small (19 KB) instead of a full 150 KB frame.
//
// The layout is deliberately independent of the band height: the header
// and every swatch straddle a 40-pixel band boundary, so each is
// assembled from two render passes.

#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/gpio>
#include <idfxx/lcd/ili9341>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/rgb565_framebuffer>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <array>
#include <chrono>
#include <string_view>

using namespace std::chrono_literals;
using namespace frequency_literals;

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

// Layout — nothing here is a multiple of BAND_H: the 48-pixel header and
// the 42-pixel swatch pitch put every element across a band boundary.
static constexpr size_t HEADER_H = 48;
static constexpr size_t SWATCH_TOP = 56;
static constexpr size_t SWATCH_PITCH = 42;

using idfxx::lcd::rgb565;
using band_canvas = idfxx::gfx::canvas<idfxx::lcd::rgb565_framebuffer>;

struct swatch {
    std::string_view name;
    rgb565 color;
};
static constexpr std::array<swatch, 6> SWATCHES{{
    {"red", {255, 0, 0}},
    {"orange", {255, 127, 0}},
    {"yellow", {255, 255, 0}},
    {"green", {0, 255, 0}},
    {"azure", {0, 127, 255}},
    {"violet", {148, 0, 211}},
}};

// Draws the whole frame in 240x320 screen coordinates; render_banded
// invokes this once per band and clips everything outside the band.
static void draw_frame(band_canvas& canvas) {
    constexpr rgb565 white(255, 255, 255);

    // Header: scaled headline on dark blue. Both the fill (rows 0-47) and
    // the 32-pixel headline (rows 10-41) cross the band boundary at y=40.
    canvas.fill_rect(0, 0, DISPLAY_W, HEADER_H, {0, 0, 96});
    canvas.draw_text(idfxx::font::spleen_8x16, 8, 10, "idfxx gfx", white, 2);

    // Framed color blocks plus labels. The 42-pixel pitch means every
    // swatch straddles a band boundary.
    for (size_t i = 0; i < SWATCHES.size(); ++i) {
        const size_t top = SWATCH_TOP + i * SWATCH_PITCH;
        canvas.fill_rect(8, top, 60, 28, SWATCHES[i].color);
        canvas.draw_rect(6, top - 2, 64, 32, white);
        canvas.draw_text(idfxx::font::spleen_8x16, 84, top + 6, SWATCHES[i].name, SWATCHES[i].color);
    }

    // Footer: separator and right-aligned status.
    canvas.draw_hline(0, DISPLAY_H - 20, DISPLAY_W, white);
    const auto status = "-72dBm 45s";
    const size_t w = idfxx::font::text_width(idfxx::font::spleen_8x16, status);
    canvas.draw_text(idfxx::font::spleen_8x16, DISPLAY_W - w - 4, DISPLAY_H - 18, status, {0, 255, 0});
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
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

        // --- Panel I/O ---
        idfxx::lcd::panel_io io(
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
        idfxx::gfx::render_banded(band, panel, DISPLAY_H, [](band_canvas& canvas) { draw_frame(canvas); });

        logger.info("Drew {} swatches", SWATCHES.size());

    } catch (const std::system_error& e) {
        logger.error("LCD error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
