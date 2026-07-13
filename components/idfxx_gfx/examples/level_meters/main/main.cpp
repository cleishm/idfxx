// SPDX-License-Identifier: Apache-2.0

// Animates level meters on an ILI9341 color LCD: six channels drawn as
// labelled horizontal bars that fill proportionally to a simulated level
// and change color at thresholds (green / amber / red). The whole 240x320
// frame is authored in screen coordinates in draw_frame(); each tick
// updates the levels and re-renders via render_banded, so the framebuffer
// stays band-sized (240x40 pixels = 19 KB) even while the whole screen
// animates.
//
// The layout is deliberately independent of the band height: the meters
// sit at a 44-pixel pitch, so every bar straddles a 40-pixel band
// boundary and is assembled from two render passes.

#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/gpio>
#include <idfxx/lcd/ili9341>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/rgb565_framebuffer>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <algorithm>
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

// Layout — nothing here is a multiple of BAND_H: the 44-pixel header and
// meter pitch put every bar across a band boundary.
static constexpr size_t HEADER_H = 44;
static constexpr size_t METER_TOP = 50;
static constexpr size_t METER_PITCH = 44;

// Bar geometry within a meter row.
static constexpr size_t BAR_X = 72;
static constexpr size_t BAR_Y = 8;
static constexpr size_t BAR_W = 160;
static constexpr size_t BAR_H = 24;

static constexpr std::array<std::string_view, 6> CHANNELS{"60Hz", "250Hz", "1kHz", "4kHz", "8kHz", "16kHz"};

// Random-walk level simulation, 0-100 per channel (a simple LCG supplies
// the steps).
static int next_level(size_t channel) {
    static uint32_t state = 12345;
    static std::array<int, CHANNELS.size()> levels{60, 35, 80, 20, 55, 70};
    state = state * 1103515245u + 12345u;
    levels[channel] += static_cast<int>((state >> 16) % 15) - 7;
    return levels[channel] = std::clamp(levels[channel], 5, 100);
}

static idfxx::lcd::rgb565 level_color(int level) {
    if (level < 60) {
        return {0, 200, 0}; // green
    }
    if (level < 85) {
        return {255, 180, 0}; // amber
    }
    return {255, 40, 40}; // red
}

using idfxx::lcd::rgb565;
using band_canvas = idfxx::gfx::canvas<idfxx::lcd::rgb565_framebuffer>;

// Draws the whole frame in 240x320 screen coordinates; render_banded
// invokes this once per band, so it must be a pure function of the levels
// (which is why each tick samples them before rendering).
static void draw_frame(band_canvas& canvas, const std::array<int, CHANNELS.size()>& levels) {
    constexpr rgb565 white(255, 255, 255);
    constexpr rgb565 gray(128, 128, 128);

    // Header: scaled title on dark blue. Both the fill (rows 0-43) and the
    // 32-pixel title (rows 10-41) cross the band boundary at y=40.
    canvas.fill_rect(0, 0, DISPLAY_W, HEADER_H, {0, 0, 96});
    canvas.draw_text(idfxx::font::spleen_8x16, 8, 10, "level meters", white, 2);

    // One labelled meter per channel. The 44-pixel pitch means every bar
    // straddles a band boundary.
    for (size_t i = 0; i < CHANNELS.size(); ++i) {
        const size_t top = METER_TOP + i * METER_PITCH;
        const int level = levels[i];
        canvas.draw_text(idfxx::font::spleen_8x16, 8, top + 12, CHANNELS[i], white);
        canvas.draw_rect(BAR_X, top + BAR_Y, BAR_W, BAR_H, white);
        canvas.fill_rect(
            BAR_X + 2, top + BAR_Y + 2, static_cast<size_t>(level) * (BAR_W - 4) / 100, BAR_H - 4, level_color(level)
        );
        for (size_t tick = 1; tick < 4; ++tick) {
            canvas.draw_vline(BAR_X + tick * BAR_W / 4, top + BAR_Y + BAR_H + 2, 4, gray);
        }
    }
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

        while (true) {
            // Advance the simulation, then render the frame it describes.
            std::array<int, CHANNELS.size()> levels;
            for (size_t i = 0; i < CHANNELS.size(); ++i) {
                levels[i] = next_level(i);
            }

            idfxx::gfx::render_banded(band, panel, DISPLAY_H, [&](band_canvas& canvas) { draw_frame(canvas, levels); });
            idfxx::delay(80ms);
        }
    } catch (const std::system_error& e) {
        logger.error("LCD error: {}", e.what());
    }
}
