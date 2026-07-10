// SPDX-License-Identifier: Apache-2.0

// Plots a live scrolling line chart on an SSD1306 OLED: a simulated
// temperature reading joined into a polyline with draw_line, inside a
// framed chart area with a dotted midline, plus a text readout that
// updates each frame. The whole frame is cleared and redrawn each tick —
// the simplest animation pattern.

#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/i2c/master>
#include <idfxx/lcd/mono_framebuffer>
#include <idfxx/lcd/panel_io>
#include <idfxx/lcd/ssd1306>
#include <idfxx/log>
#include <idfxx/sched>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>

using namespace std::chrono_literals;
using namespace frequency_literals;

static constexpr idfxx::log::logger logger{"example"};

// Pin assignments — change to match your board.
static constexpr auto PIN_SDA = idfxx::gpio_8;
static constexpr auto PIN_SCL = idfxx::gpio_9;

// Chart area, including its one-pixel frame.
static constexpr size_t CHART_Y = 12;
static constexpr size_t CHART_W = 128;
static constexpr size_t CHART_H = 52;

// Simulated reading range, in tenths of a degree.
static constexpr int READING_MIN = 180; // 18.0 C
static constexpr int READING_MAX = 300; // 30.0 C

// One sample every two columns across the chart interior.
static constexpr size_t SAMPLES = (CHART_W - 2) / 2;

// Random-walk temperature simulation (a simple LCG supplies the steps).
static int next_reading() {
    static uint32_t state = 12345;
    static int reading = 237;
    state = state * 1103515245u + 12345u;
    reading += static_cast<int>((state >> 16) % 7) - 3;
    return reading = std::clamp(reading, READING_MIN, READING_MAX);
}

// Maps a reading to a chart row, larger values higher up.
static size_t sample_y(int reading) {
    constexpr size_t usable = CHART_H - 3;
    return CHART_Y + 1 + usable - static_cast<size_t>(reading - READING_MIN) * usable / (READING_MAX - READING_MIN);
}

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

        std::array<int, SAMPLES> samples;
        samples.fill(next_reading());
        logger.info("charting {} samples at 10 fps", samples.size());

        while (true) {
            std::shift_left(samples.begin(), samples.end(), 1);
            samples.back() = next_reading();

            canvas.clear();

            // Text readout above the chart.
            const auto readout = std::format("temp {}.{}C", samples.back() / 10, samples.back() % 10);
            canvas.draw_text(idfxx::font::spleen_5x8, 0, 1, readout);

            // Chart frame with a dotted midline.
            canvas.draw_rect(0, CHART_Y, CHART_W, CHART_H, true);
            for (size_t x = 2; x < CHART_W - 4; x += 8) {
                canvas.draw_hline(x, CHART_Y + CHART_H / 2, 3, true);
            }

            // The samples, joined into a polyline.
            for (size_t i = 1; i < samples.size(); ++i) {
                canvas.draw_line(2 * i - 1, sample_y(samples[i - 1]), 2 * i + 1, sample_y(samples[i]), true);
            }

            canvas.flush(display);
            idfxx::delay(100ms);
        }
    } catch (const std::system_error& e) {
        logger.error("failed: {}", e.what());
    }
}
