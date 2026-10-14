// SPDX-License-Identifier: Apache-2.0

// Partial refreshes across ESP32 deep sleep on the Seeed Studio XIAO ePaper
// Display Board EE05 (ESP32-S3 Plus) with the 2.13" 122x250 SSD1680 panel.
//
// Each boot draws a wake counter, refreshes the panel, and deep-sleeps on a
// timer. A deep sleep is a reboot: the driver starts with no partial-refresh
// baseline, so its first partial refresh would be promoted to a full one.
// To keep updates partial, the frame on the glass is saved in RTC memory
// before sleeping and handed back with restore_baseline after the wake.
// Every FULL_REFRESH_INTERVAL wakes a full refresh clears accumulated
// ghosting.
//
// RTC memory survives deep sleep, and the 122x250 mono frame is only ~4 KB,
// so it fits there. Larger panels do not: a 7.5" 800x480 UC8179 frame is
// 48 KB, more than the RTC memory holds. Persist such frames elsewhere —
// flash (LittleFS/SPIFFS, or an NVS partition sized for it) or an SD card —
// with a validity check such as a CRC, and mind flash wear when saving on
// every wake.
//
// The saved frame must match the glass. A mismatch does no damage, but the
// differing pixels are not driven correctly until the next full refresh.

#include <idfxx/epaper/mono_framebuffer>
#include <idfxx/epaper/ssd1680>
#include <idfxx/font/spleen>
#include <idfxx/gfx>
#include <idfxx/gpio>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/sleep>
#include <idfxx/spi/master>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <esp_attr.h>
#include <format>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"deep_sleep_partial"};

// EE05 board wiring (from Seeed's board support files).
static constexpr auto PIN_SCLK = idfxx::gpio_7;
static constexpr auto PIN_MOSI = idfxx::gpio_9; // no MISO: the panel is write-only
static constexpr auto PIN_CS = idfxx::gpio_44;
static constexpr auto PIN_DC = idfxx::gpio_10;
static constexpr auto PIN_RST = idfxx::gpio_38;
static constexpr auto PIN_BUSY = idfxx::gpio_4;
static constexpr auto PIN_POWER = idfxx::gpio_43; // Display power enable, active-high

// The EE05's 2.13" panel.
static constexpr size_t PANEL_WIDTH = 122;
static constexpr size_t PANEL_HEIGHT = 250;

// Time spent in deep sleep between updates.
static constexpr auto SLEEP_INTERVAL = 30s;

// Partial refreshes accumulate ghosting; every this many wakes, run a full
// refresh instead.
static constexpr uint32_t FULL_REFRESH_INTERVAL = 10;

// mono_framebuffer layout: rows padded to whole bytes, one bit per pixel.
static constexpr size_t FRAME_STRIDE = (PANEL_WIDTH + 7) / 8;

// The frame on the glass, saved across deep sleep in RTC memory. RTC_DATA_ATTR
// variables keep their values through deep sleep and are reinitialized on
// every other boot (power-on, reset, panic). Only 4000 bytes for this panel;
// see the top of the file for larger panels.
RTC_DATA_ATTR static std::array<uint8_t, FRAME_STRIDE * PANEL_HEIGHT> saved_frame;

// Whether saved_frame holds the image on the glass: set only once a run has
// refreshed the panel and saved its frame, so it stays false after a fresh
// boot or a run that failed before the save.
RTC_DATA_ATTR static bool saved_frame_valid = false;

// Wakes since the last power-on or reset.
RTC_DATA_ATTR static uint32_t wake_count;

using idfxx::epaper::refresh_mode;

namespace {

constexpr size_t counter_y = 76;
constexpr size_t counter_h = 32;
constexpr size_t bar_x = 4;
constexpr size_t bar_y = 134;
constexpr size_t bar_w = PANEL_WIDTH - 2 * bar_x;
constexpr size_t bar_h = 12;

// The parts of the screen that never change, drawn only after a power-on or
// reset. After a deep sleep they are already in the restored frame.
void draw_static_layout(idfxx::gfx::canvas<idfxx::epaper::mono_framebuffer>& canvas) {
    canvas.clear();
    canvas.fill_rect(0, 0, canvas.width(), 14, true);
    canvas.draw_text(idfxx::font::spleen_5x8, 3, 3, "idfxx epaper", false);
    canvas.draw_text(idfxx::font::spleen_8x16, 4, 22, "deep sleep", true);
    canvas.draw_text(idfxx::font::spleen_5x8, 4, 42, "partial refresh", true);
    canvas.draw_hline(0, 56, canvas.width(), true);
    canvas.draw_text(idfxx::font::spleen_5x8, 4, 64, "wakes", true);
    canvas.draw_text(idfxx::font::spleen_5x8, 4, 122, "next full refresh", true);
    canvas.draw_rect(bar_x, bar_y, bar_w, bar_h, true);
}

// The parts that change on every wake: the counter, and a bar filling up
// towards the next full refresh.
void draw_wake_count(idfxx::gfx::canvas<idfxx::epaper::mono_framebuffer>& canvas, uint32_t count) {
    canvas.fill_rect(0, counter_y, canvas.width(), counter_h, false);
    canvas.draw_text(idfxx::font::spleen_8x16, 8, counter_y, std::format("{}", count), true, 2);

    const size_t inner_w = bar_w - 4;
    const size_t filled = inner_w * (count % FULL_REFRESH_INTERVAL) / (FULL_REFRESH_INTERVAL - 1);
    canvas.fill_rect(bar_x + 2, bar_y + 2, inner_w, bar_h - 4, false);
    canvas.fill_rect(bar_x + 2, bar_y + 2, filled, bar_h - 4, true);
}

} // namespace

extern "C" void app_main() {
    // Invalidate the saved frame now, so that a failure before the next save
    // does not leave a stale frame for the following wake.
    const bool resume = saved_frame_valid;
    saved_frame_valid = false;

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
                .width = PANEL_WIDTH,
                .height = PANEL_HEIGHT,
                .reset_gpio = PIN_RST,
                .busy_gpio = PIN_BUSY,
            }
        );

        idfxx::epaper::mono_framebuffer fb(display.width(), display.height());
        idfxx::gfx::canvas canvas(fb);
        // saved_frame is sized for the EE05 panel: resize it along with any
        // change to the panel dimensions.
        assert(fb.data().size() == saved_frame.size());

        if (resume) {
            // The panel still shows the frame saved before the deep sleep:
            // load it and tell the driver, so the next partial refresh stays
            // partial.
            std::ranges::copy(saved_frame, fb.data().begin());
            display.restore_baseline(fb);
            ++wake_count;
            logger.info("woke from deep sleep (wake {}); baseline restored", wake_count);
        } else {
            wake_count = 0;
            draw_static_layout(canvas);
            logger.info("power-on or reset; starting from a cleared frame");
        }

        // --- Update the screen ---
        draw_wake_count(canvas, wake_count);
        display.write(fb);
        // The first boot has no baseline and is promoted to full anyway.
        const bool full = !resume || wake_count % FULL_REFRESH_INTERVAL == 0;
        display.refresh(full ? refresh_mode::full : refresh_mode::partial);
        logger.info("wake {}: {} refresh", wake_count, full ? "full" : "partial");

        // --- Save the frame on the glass, then sleep ---
        std::ranges::copy(fb.data(), saved_frame.begin());
        display.sleep();
        saved_frame_valid = true;
    } catch (const std::system_error& e) {
        // Sleep anyway rather than staying awake on battery. The saved frame
        // was invalidated above, so the next wake redraws from scratch.
        logger.error("ePaper error: {}", e.what());
    }

    logger.info("deep sleeping for {}", SLEEP_INTERVAL);
    idfxx::sleep::deep_sleep(idfxx::sleep::timer_wake{SLEEP_INTERVAL});
}
