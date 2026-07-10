// SPDX-License-Identifier: Apache-2.0

// Bounces a spinning square around an ILI9341 color LCD: a rotating square
// drifts across the whole 240x320 screen and reflects off each edge,
// changing color on every bounce. The square is drawn as four lines between
// its rotated corners, authored once in screen coordinates in draw_frame();
// render_banded runs it once per 240x40 band and flushes each slice, so the
// framebuffer stays band-sized (19 KB) even though the square roams the
// whole display.
//
// Nothing here is band-aligned: the square sits at an arbitrary position
// every frame and almost always straddles a band boundary, so it is
// assembled from the two (or three) passes it happens to cross — exactly
// what render_banded's four-side clipping is for.

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
#include <cmath>
#include <numbers>

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

// Motion. The square is bounded by its circumscribed circle of radius
// SQUARE_R (side ~= SQUARE_R * sqrt(2)), so keeping the center at least
// SQUARE_R from every edge keeps the whole square on screen at any rotation.
static constexpr float SQUARE_R = 34.0f;
static constexpr float SPEED_X = 2.7f;  // pixels per tick
static constexpr float SPEED_Y = 1.9f;  // pixels per tick
static constexpr float ANG_VEL = 0.05f; // radians per tick

using idfxx::lcd::rgb565;
using band_canvas = idfxx::gfx::canvas<idfxx::lcd::rgb565_framebuffer>;

// Cycled through on each bounce so collisions are visible.
static constexpr std::array<rgb565, 6> PALETTE{{
    {0, 220, 255},  // cyan
    {0, 255, 120},  // green
    {255, 220, 0},  // yellow
    {255, 90, 0},   // orange
    {255, 40, 120}, // pink
    {180, 90, 255}, // violet
}};

struct point {
    size_t x, y;
};

// Everything the drawing pass needs, derived from the simulation each tick
// so draw_frame stays a pure function invoked once per band.
struct frame {
    std::array<point, 4> corners;
    rgb565 color;
};

// Mutable simulation state, advanced once per tick.
struct square {
    float cx = DISPLAY_W / 2.0f;
    float cy = DISPLAY_H / 2.0f;
    float vx = SPEED_X;
    float vy = SPEED_Y;
    float angle = 0.0f;
    size_t color = 0;
};

// Steps the square forward and reflects it off the display edges, advancing
// to the next palette color whenever it bounces.
static void advance(square& sq) {
    sq.cx += sq.vx;
    sq.cy += sq.vy;
    sq.angle += ANG_VEL;

    bool bounced = false;
    if (sq.cx < SQUARE_R) {
        sq.cx = SQUARE_R;
        sq.vx = -sq.vx;
        bounced = true;
    } else if (sq.cx > DISPLAY_W - SQUARE_R) {
        sq.cx = DISPLAY_W - SQUARE_R;
        sq.vx = -sq.vx;
        bounced = true;
    }
    if (sq.cy < SQUARE_R) {
        sq.cy = SQUARE_R;
        sq.vy = -sq.vy;
        bounced = true;
    } else if (sq.cy > DISPLAY_H - SQUARE_R) {
        sq.cy = DISPLAY_H - SQUARE_R;
        sq.vy = -sq.vy;
        bounced = true;
    }

    if (bounced) {
        sq.color = (sq.color + 1) % PALETTE.size();
    }
}

// Rounds a screen coordinate to a pixel index (negatives clamp to 0; the
// canvas clips the far edges).
static size_t to_pixel(float v) {
    return static_cast<size_t>(std::lround(std::max(0.0f, v)));
}

// Samples the four rotated corners and the current color into a frame.
static frame snapshot(const square& sq) {
    constexpr float quarter = std::numbers::pi_v<float> / 2.0f;
    constexpr float eighth = std::numbers::pi_v<float> / 4.0f;

    frame f{};
    for (size_t k = 0; k < f.corners.size(); ++k) {
        const float a = sq.angle + eighth + k * quarter;
        f.corners[k] = {to_pixel(sq.cx + SQUARE_R * std::cos(a)), to_pixel(sq.cy + SQUARE_R * std::sin(a))};
    }
    f.color = PALETTE[sq.color];
    return f;
}

// Draws the whole frame in 240x320 screen coordinates; render_banded invokes
// this once per band and clips everything outside the band.
static void draw_frame(band_canvas& canvas, const frame& f) {
    constexpr rgb565 border(48, 48, 48);
    constexpr rgb565 lead(255, 255, 255);

    // Dim frame marking the play area, straddling several band boundaries.
    canvas.draw_rect(0, 0, DISPLAY_W, DISPLAY_H, border);

    // The square: one line per edge between successive corners. The leading
    // edge is white so the spin is easy to follow.
    for (size_t k = 0; k < f.corners.size(); ++k) {
        const point& a = f.corners[k];
        const point& b = f.corners[(k + 1) % f.corners.size()];
        canvas.draw_line(a.x, a.y, b.x, b.y, k == 0 ? lead : f.color);
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
        square sq;

        while (true) {
            // Advance the simulation, then render the frame it describes.
            advance(sq);
            const frame f = snapshot(sq);

            idfxx::gfx::render_banded(band, panel, DISPLAY_H, [&](band_canvas& canvas) { draw_frame(canvas, f); });
            idfxx::delay(20ms);
        }
    } catch (const std::system_error& e) {
        logger.error("LCD error: {}", e.what());
    }
}
