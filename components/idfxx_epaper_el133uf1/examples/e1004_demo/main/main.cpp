// SPDX-License-Identifier: Apache-2.0

// Demo for the Seeed Studio reTerminal E1004 (ESP32-S3) with its 13.3"
// 1200x1600 Spectra 6 T133A01 module (E Ink EL133UF1 glass). Shows a
// six-color screen drawn through a full PSRAM framebuffer in landscape,
// then a monochrome text page rendered band-by-band (a 1200x100 band
// instead of a full frame), then a closing screen, and puts the panel to
// deep sleep. Each full refresh takes 25-30 seconds.

#include <idfxx/epaper/color6_framebuffer>
#include <idfxx/epaper/el133uf1>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <array>
#include <chrono>
#include <format>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"e1004_demo"};

// reTerminal E1004 wiring (from Seeed's board support files). The panel's
// two controllers share the bus and D/C line and have separate chip
// selects: CS for the master (left half), CS1 for the slave (right half).
static constexpr auto PIN_SCLK = idfxx::gpio_7;
static constexpr auto PIN_MOSI = idfxx::gpio_9;
static constexpr auto PIN_MISO = idfxx::gpio_8; // Panel data output, for the presence check
static constexpr auto PIN_CS = idfxx::gpio_10;
static constexpr auto PIN_CS1 = idfxx::gpio_2;
static constexpr auto PIN_DC = idfxx::gpio_11;
static constexpr auto PIN_RST = idfxx::gpio_38;
static constexpr auto PIN_BUSY = idfxx::gpio_13;
static constexpr auto PIN_POWER = idfxx::gpio_12; // Display power enable, active-high
static constexpr auto PIN_SD_CS = idfxx::gpio_14; // microSD chip select, shares MISO with the panel

using idfxx::epaper::color6;
using idfxx::epaper::color_mode;

namespace {

// Stage 1: six color bars with a caption on each, drawn in landscape
// through a rotated view of the portrait-native framebuffer.
template<typename Canvas>
void draw_color_screen(Canvas& canvas) {
    constexpr std::array bars{
        std::pair{color6::black, "black"},
        std::pair{color6::white, "white"},
        std::pair{color6::yellow, "yellow"},
        std::pair{color6::red, "red"},
        std::pair{color6::blue, "blue"},
        std::pair{color6::green, "green"},
    };
    const size_t bar_w = canvas.width() / bars.size();
    for (size_t i = 0; i < bars.size(); ++i) {
        const auto [color, name] = bars[i];
        // The last bar absorbs the remainder of the integer division.
        const size_t w = i + 1 == bars.size() ? canvas.width() - i * bar_w : bar_w;
        canvas.fill_rect(i * bar_w, 0, w, canvas.height(), color);
        // Caption in a contrasting color at the top of each bar.
        const color6 ink = color == color6::black ? color6::white : color6::black;
        canvas.draw_text(idfxx::font::spleen_8x16, i * bar_w + 16, 24, name, ink, 3);
    }
    canvas.fill_rect(0, canvas.height() - 120, canvas.width(), 120, color6::white);
    canvas.draw_text(idfxx::font::spleen_8x16, 24, canvas.height() - 96, "idfxx epaper", color6::black, 4);
    canvas.draw_text(
        idfxx::font::spleen_8x16, 24, canvas.height() - 40, "EL133UF1 1200x1600 Spectra 6", color6::red, 2
    );
}

// Stages 2 and 3: monochrome pages drawn through a band-sized framebuffer
// with idfxx::gfx::render_banded — the draw function is invoked once per
// band and describes the complete 1200x1600 (portrait) frame each time.
void draw_text_page(idfxx::gfx::canvas<idfxx::epaper::mono_framebuffer>& canvas, bool closing) {
    canvas.fill_rect(0, 0, 1200, 96, true);
    canvas.draw_text(idfxx::font::spleen_8x16, 32, 24, "idfxx epaper", false, 3);
    if (closing) {
        canvas.draw_text(idfxx::font::spleen_8x16, 32, 200, "demo complete", true, 6);
        return;
    }
    canvas.draw_rect(32, 160, 1136, 400, true);
    canvas.draw_text(idfxx::font::spleen_8x16, 64, 224, "hello", true, 12);
    canvas.draw_text(idfxx::font::spleen_8x16, 32, 640, "monochrome, banded", true, 4);
    for (int i = 0; i < 12; ++i) {
        canvas.draw_text(idfxx::font::spleen_8x16, 32, 760 + i * 48, std::format("line {:02}", i), true, 2);
    }
    canvas.draw_hline(0, 1500, 1200, true);
}

} // namespace

extern "C" void app_main() {
    try {
        // --- Display power rail ---
        idfxx::gpio power = PIN_POWER;
        power.set_direction(idfxx::gpio::mode::output);
        power.set_level(idfxx::gpio::level::high);
        idfxx::delay(100ms);

        // --- SPI bus ---
        // The microSD card shares MISO with the panel: keep it deselected so
        // it leaves the line to the panel's presence check.
        idfxx::gpio sd_cs = PIN_SD_CS;
        sd_cs.set_level(idfxx::gpio::level::high);
        sd_cs.set_direction(idfxx::gpio::mode::output);

        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.miso = PIN_MISO;
        bus_cfg.sclk = PIN_SCLK;
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

        // --- Display driver (drives both chip selects and D/C itself) ---
        idfxx::epaper::el133uf1 display(
            bus,
            {
                .master_cs_gpio = PIN_CS,
                .slave_cs_gpio = PIN_CS1,
                .dc_gpio = PIN_DC,
                .reset_gpio = PIN_RST,
                .busy_gpio = PIN_BUSY,
                .check_presence = true,
            }
        );
        logger.info("display initialized ({}x{})", display.width(), display.height());

        // --- Stage 1: six-color screen through a full (PSRAM) framebuffer ---
        // The panel is portrait-native; the rotated view presents it as a
        // 1600x1200 landscape surface.
        {
            idfxx::epaper::color6_framebuffer fb(display.width(), display.height());
            idfxx::gfx::rotated view(fb, idfxx::gfx::rotation::cw90);
            idfxx::gfx::canvas canvas(view);
            draw_color_screen(canvas);
            display.write(fb);
        }
        display.refresh();
        logger.info("stage 1: six-color refresh done");
        idfxx::delay(10s);

        // --- Stage 2: monochrome text page, rendered in 1200x100 bands ---
        display.set_color_mode(color_mode::mono);
        idfxx::epaper::mono_framebuffer band(display.width(), 100);
        idfxx::gfx::render_banded(band, display, display.height(), [](auto& canvas) { draw_text_page(canvas, false); });
        display.refresh();
        logger.info("stage 2: monochrome refresh done");
        idfxx::delay(10s);

        // --- Stage 3: closing screen ---
        idfxx::gfx::render_banded(band, display, display.height(), [](auto& canvas) { draw_text_page(canvas, true); });
        display.refresh();
        logger.info("stage 3: closing screen done");

        // --- Deep sleep: the image persists without power ---
        display.sleep();
        logger.info("display sleeping; demo complete");

    } catch (const std::system_error& e) {
        logger.error("ePaper error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
