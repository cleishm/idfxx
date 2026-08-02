// SPDX-License-Identifier: Apache-2.0

// Demo for the Seeed Studio XIAO ePaper Display Board EE05 (ESP32-S3 Plus)
// with the 2.13" 122x250 SSD1680 panel. Walks through the driver's refresh
// modes: a full-refresh title screen, a 4-level grayscale screen (run early,
// while the glass is fresh — see the idfxx_epaper_uc8179 README on gray4
// ghosting), a fast refresh, and a partial-refresh counter loop, then shows
// a closing screen and puts the panel to deep sleep.

#include <idfxx/epaper/gray4_framebuffer>
#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/ssd1680>
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

static constexpr idfxx::log::logger logger{"ee05_demo"};

// EE05 board wiring (from Seeed's board support files).
static constexpr auto PIN_SCLK = idfxx::gpio_7;
static constexpr auto PIN_MOSI = idfxx::gpio_9; // no MISO: the panel is write-only
static constexpr auto PIN_CS = idfxx::gpio_44;
static constexpr auto PIN_DC = idfxx::gpio_10;
static constexpr auto PIN_RST = idfxx::gpio_38;
static constexpr auto PIN_BUSY = idfxx::gpio_4;
static constexpr auto PIN_POWER = idfxx::gpio_43; // Display power enable, active-high

using idfxx::epaper::color_mode;
using idfxx::epaper::gray4;
using idfxx::epaper::refresh_mode;

namespace {

// Stages 1 and 3: a title screen drawn with idfxx_gfx and flushed as one
// full refresh.
void draw_title_screen(idfxx::gfx::canvas<idfxx::epaper::mono_framebuffer>& canvas) {
    canvas.clear();
    canvas.fill_rect(0, 0, canvas.width(), 14, true);
    canvas.draw_text(idfxx::font::spleen_5x8, 3, 3, "idfxx epaper", false);
    canvas.draw_rect(2, 20, canvas.width() - 4, 40, true);
    canvas.draw_text(idfxx::font::spleen_8x16, 8, 30, "hello", true, 2);
    canvas.draw_text(idfxx::font::spleen_5x8, 4, 70, "SSD1680 122x250", true);
    canvas.draw_hline(0, 84, canvas.width(), true);
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
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.sclk = PIN_SCLK;
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

        // --- Panel I/O and driver ---
        idfxx::panel_io io(bus, idfxx::epaper::ssd1680::spi_io_config(PIN_CS, PIN_DC));
        idfxx::epaper::ssd1680 display(
            io,
            {
                .reset_gpio = PIN_RST,
                .busy_gpio = PIN_BUSY,
            }
        );
        logger.info("display initialized ({}x{})", display.width(), display.height());

        idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
        idfxx::gfx::canvas canvas(fb);

        // --- Stage 1: full-refresh title screen ---
        draw_title_screen(canvas);
        canvas.flush(display);
        display.refresh();
        logger.info("stage 1: full refresh done");
        idfxx::delay(5s);

        // --- Stage 2: 4-level grayscale ---
        // The gray midtones are open-loop drives that amplify residual
        // polarization from long-displayed content, so run the grayscale
        // stage early, while the glass is fresh (see the idfxx_epaper_uc8179
        // README on gray4 ghosting). Grayscale waveforms also erase less
        // aggressively than a mono full refresh, so clear to white first to
        // keep the previous screen from ghosting through the gray bands.
        display.clear();
        display.refresh();
        display.set_color_mode(color_mode::gray4);
        {
            idfxx::epaper::gray4_framebuffer gray_fb(display.width(), display.height());
            idfxx::gfx::canvas gray_canvas(gray_fb);
            constexpr std::array levels{gray4::white, gray4::light, gray4::dark, gray4::black};
            const size_t band_h = gray_fb.height() / levels.size();
            for (size_t i = 0; i < levels.size(); ++i) {
                gray_canvas.fill_rect(0, i * band_h, gray_canvas.width(), band_h, levels[i]);
            }
            gray_canvas.flush(display);
        }
        display.refresh();
        logger.info("stage 2: grayscale done");
        idfxx::delay(5s);

        // --- Stage 3: back to monochrome ---
        display.set_color_mode(color_mode::mono);
        draw_title_screen(canvas);
        canvas.flush(display);
        display.refresh();
        logger.info("stage 3: full refresh done");
        idfxx::delay(5s);

        // --- Stage 4: fast refresh ---
        canvas.draw_text(idfxx::font::spleen_5x8, 4, 90, "fast refresh", true);
        canvas.flush(display);
        display.refresh(refresh_mode::fast);
        logger.info("stage 4: fast refresh done");
        idfxx::delay(5s);

        // --- Stage 5: partial-refresh counter loop ---
        // Fast-refresh output is weakly driven and would settle to gray
        // under partial refreshes, so start the sequence from a full
        // refresh, which drives pixels to their stable extremes.
        display.refresh();
        constexpr size_t counter_y = 110;
        constexpr size_t counter_h = 32;
        for (int i = 0; i <= 20; ++i) {
            canvas.fill_rect(0, counter_y, canvas.width(), counter_h, false);
            canvas.draw_text(idfxx::font::spleen_8x16, 8, counter_y, std::format("{:02}", i), true, 2);
            fb.flush_rows(display, counter_y, counter_y + counter_h);
            // Partial updates accumulate ghosting; clean up with a full
            // refresh every 10 iterations.
            display.refresh(i % 10 == 9 ? refresh_mode::full : refresh_mode::partial);
        }
        logger.info("stage 5: partial refresh loop done");
        idfxx::delay(5s);

        // --- Stage 6: closing screen ---
        canvas.clear();
        canvas.fill_rect(0, 0, canvas.width(), 14, true);
        canvas.draw_text(idfxx::font::spleen_5x8, 3, 3, "idfxx epaper", false);
        canvas.draw_text(idfxx::font::spleen_8x16, 8, 100, "demo", true, 2);
        canvas.draw_text(idfxx::font::spleen_8x16, 8, 136, "complete", true);
        canvas.flush(display);
        display.refresh();
        logger.info("stage 6: closing screen done");

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
