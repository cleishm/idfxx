// SPDX-License-Identifier: Apache-2.0

#include <idfxx/byte_source>
#include <idfxx/gpio>
#include <idfxx/image>
#include <idfxx/jpeg>
#include <idfxx/lcd/color>
#include <idfxx/lcd/ili9341>
#include <idfxx/log>
#include <idfxx/memory>
#include <idfxx/panel_io>
#include <idfxx/sched>
#include <idfxx/spi/master>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using namespace quanta::frequency_literals;

namespace image = idfxx::image;

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

// How to turn the upright picture to fit the panel's native 240x320 portrait layout (row 0 at the top).
// rotate_90 turns it a quarter turn clockwise, so it shows upright in landscape with the display turned a
// quarter turn counter-clockwise (its native top edge on the left). rotate_270 suits the display turned
// clockwise instead, and identity shows the picture in portrait, cropped to fill the screen.
static constexpr image::orientation PANEL_ROTATION = image::orientation::rotate_90;

using idfxx::lcd::rgb565;

// The photo, embedded by main/CMakeLists.txt.
extern const uint8_t photo_jpg_start[] asm("_binary_photo_jpg_start");
extern const uint8_t photo_jpg_end[] asm("_binary_photo_jpg_end");

// Pixels on their way to the panel, in internal, DMA-capable RAM that the SPI transfer can read directly. With
// PSRAM enabled, a plain vector this size could land in PSRAM instead.
using draw_buffer = std::vector<rgb565, idfxx::dma_allocator<rgb565>>;

// Converts a decoded pixel to the panel's format.
static constexpr rgb565 to_rgb565(image::rgb888 pixel) {
    return rgb565(pixel.r, pixel.g, pixel.b);
}

// Where the photo goes on the screen: the part of it that fits, and where that part lands.
struct placement {
    size_t photo_x; // top-left corner of the visible part, in the turned photo
    size_t photo_y;
    size_t width; // size of the visible part
    size_t height;
    size_t screen_x; // where the visible part's top-left corner lands on the screen
    size_t screen_y;
};

// Centres a photo on the screen: crops whatever overhangs it, and leaves black borders where the photo is smaller
// (the decoder never scales up).
static placement centre_on_screen(size_t photo_width, size_t photo_height) {
    const size_t width = std::min(photo_width, DISPLAY_W);
    const size_t height = std::min(photo_height, DISPLAY_H);
    return {
        .photo_x = (photo_width - width) / 2,
        .photo_y = (photo_height - height) / 2,
        .width = width,
        .height = height,
        .screen_x = (DISPLAY_W - width) / 2,
        .screen_y = (DISPLAY_H - height) / 2,
    };
}

// Fills the screen with black, a band at a time.
static void clear_screen(idfxx::lcd::panel& panel) {
    const draw_buffer black(DISPLAY_W * BAND_H);
    for (size_t y = 0; y < DISPLAY_H; y += BAND_H) {
        panel.draw_bitmap(0, y, DISPLAY_W, y + BAND_H, black.data());
    }
}

// Draws a picture with its top-left corner at (x, y) on the screen, converting it to RGB565 a band of rows at a time.
static void draw_in_bands(idfxx::lcd::panel& panel, image::view<const image::rgb888> picture, size_t x, size_t y) {
    draw_buffer band(picture.width() * BAND_H);
    for (size_t top = 0; top < picture.height(); top += BAND_H) {
        const size_t rows = std::min(BAND_H, picture.height() - top);
        for (size_t i = 0; i < rows; ++i) {
            const auto band_row = band.begin() + i * picture.width();
            std::ranges::transform(picture.row(top + i), band_row, to_rgb565);
        }
        panel.draw_bitmap(x, y + top, x + picture.width(), y + top + rows, band.data());
    }
}

// Decodes a JPEG and shows it filling the screen, turned upright and then by panel_rotation.
static void show_photo(idfxx::lcd::panel& panel, std::span<const uint8_t> jpeg, image::orientation panel_rotation) {
    idfxx::memory_source source{jpeg};
    idfxx::jpeg::decoder decoder(source);

    // One transform takes the decoded pixels to the panel: the photo's EXIF orientation turns it upright, then
    // panel_rotation turns it to fit the panel.
    const image::orientation transform = image::compose(decoder.info().orientation, panel_rotation);
    const bool turned = image::swaps_axes(transform); // a quarter turn, which swaps width and height

    // Decode at the smallest scale that still covers the screen. The target is in the photo's stored
    // orientation, so swap it when the photo is turned.
    const size_t cover_width = turned ? DISPLAY_H : DISPLAY_W;
    const size_t cover_height = turned ? DISPLAY_W : DISPLAY_H;
    decoder.scale_to_cover(cover_width, cover_height);
    logger.info(
        "Photo is {}x{} with EXIF orientation {}; decoding at {}x{}",
        decoder.info().width,
        decoder.info().height,
        std::to_underlying(decoder.info().orientation),
        decoder.output_width(),
        decoder.output_height()
    );

    // Decode the whole photo into PSRAM, then turn it for the panel (also in PSRAM).
    image::buffer<image::rgb888, idfxx::spiram_allocator<image::rgb888>> decoded(
        decoder.output_width(), decoder.output_height()
    );
    decoder.decode(decoded);
    const auto photo = image::oriented(decoded, transform);

    const placement place = centre_on_screen(photo.width(), photo.height());
    const image::view<const image::rgb888> visible =
        photo.subview(place.photo_x, place.photo_y, place.width, place.height);

    clear_screen(panel);
    draw_in_bands(panel, visible, place.screen_x, place.screen_y);
}

extern "C" void app_main() {
    try {
        // --- Backlight on ---
        idfxx::gpio backlight = PIN_BL;
        idfxx::configure_gpios({.mode = idfxx::gpio::mode::output}, backlight);
        backlight.set_level(idfxx::gpio::level::high);

        // --- SPI bus ---
        // The largest single draw is a full-width band, as when clearing the screen.
        idfxx::spi::bus_config bus_cfg{};
        bus_cfg.mosi = PIN_MOSI;
        bus_cfg.sclk = PIN_SCLK;
        bus_cfg.max_transfer_sz = DISPLAY_W * BAND_H * sizeof(rgb565);
        idfxx::spi::master_bus bus(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);

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

        // --- Show the photo ---
        show_photo(panel, {photo_jpg_start, photo_jpg_end}, PANEL_ROTATION);
        logger.info("Photo shown");

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}
