// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd ili9341
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/ili9341"
#include "idfxx/lcd/rgb565_framebuffer"
#include "unity.h"

#include <atomic>
#include <type_traits>

using namespace idfxx::lcd;
using namespace quanta::frequency_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// panel::config is default-constructible
static_assert(std::is_default_constructible_v<panel::config>);

// ili9341 is non-copyable
static_assert(!std::is_copy_constructible_v<ili9341>);
static_assert(!std::is_copy_assignable_v<ili9341>);

// ili9341 is move-only
static_assert(std::is_move_constructible_v<ili9341>);
static_assert(std::is_move_assignable_v<ili9341>);

// ili9341 inherits from panel base class
static_assert(std::is_base_of_v<panel, ili9341>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("panel::config struct initialization", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config{
        .reset_gpio = gpio_4,
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    TEST_ASSERT_EQUAL(4, config.reset_gpio.num());
    TEST_ASSERT_EQUAL(rgb_element_order::bgr, config.rgb_element_order);
    TEST_ASSERT_EQUAL(16, config.bits_per_pixel);
}

TEST_CASE("panel::config default reset_gpio is NC", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config{
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    TEST_ASSERT_FALSE(config.reset_gpio.is_connected());
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.reset_gpio.num());
}


TEST_CASE("panel::config with RGB element order", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config_rgb{
        .reset_gpio = gpio::nc(),
        .rgb_element_order = rgb_element_order::rgb,
        .bits_per_pixel = 16,
    };

    panel::config config_bgr{
        .reset_gpio = gpio::nc(),
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    TEST_ASSERT_EQUAL(rgb_element_order::rgb, config_rgb.rgb_element_order);
    TEST_ASSERT_EQUAL(rgb_element_order::bgr, config_bgr.rgb_element_order);
}

TEST_CASE("panel::config with different bits per pixel", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config_16{
        .reset_gpio = gpio::nc(),
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    panel::config config_18{
        .reset_gpio = gpio::nc(),
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 18,
    };

    TEST_ASSERT_EQUAL(16, config_16.bits_per_pixel);
    TEST_ASSERT_EQUAL(18, config_18.bits_per_pixel);
}

TEST_CASE("panel::config reset_active_level default", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config{
        .reset_gpio = gpio::nc(),
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    TEST_ASSERT_EQUAL(gpio::level::low, config.reset_active_level);
}

TEST_CASE("panel::config with reset_active_level set", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config{
        .reset_gpio = gpio_4,
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
        .reset_active_level = gpio::level::high,
    };

    TEST_ASSERT_EQUAL(gpio::level::high, config.reset_active_level);
}

// Needs real SPI DMA: QEMU never completes queued color transfers, so the write would block.
TEST_CASE("ili9341 write returns after the color transfer finishes", "[idfxx][lcd][ili9341][hw]") {
    std::atomic<int> transfers_done{0};

    idfxx::spi::bus_config bus_cfg{};
    bus_cfg.mosi = idfxx::gpio_11;
    bus_cfg.miso = idfxx::gpio_13;
    bus_cfg.sclk = idfxx::gpio_12;
    auto bus = idfxx::spi::master_bus::make(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::ch_auto, bus_cfg);
    TEST_ASSERT_TRUE(bus.has_value());

    auto io = idfxx::panel_io::make(*bus, idfxx::panel_io::spi_config{
        .cs_gpio = idfxx::gpio_10,
        .dc_gpio = idfxx::gpio_4,
        .pclk_freq = 10_MHz,
        .on_color_transfer_done = [&transfers_done](esp_lcd_panel_io_event_data_t*) {
            transfers_done.fetch_add(1);
            return false;
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    });
    TEST_ASSERT_TRUE(io.has_value());

    auto display = ili9341::make(*io, {.bits_per_pixel = 16});
    TEST_ASSERT_TRUE(display.has_value());

    // A 240x40 band is ~19 KB, which takes ~15 ms to clock out at 10 MHz.
    auto band = rgb565_framebuffer::make(240, 40);
    TEST_ASSERT_TRUE(band.has_value());
    band->fill(rgb565(0, 0, 255));

    int before = transfers_done.load();
    TEST_ASSERT_TRUE(display->try_write(*band).has_value());
    // The transfer must have finished (and the buffer been released) by the time write returns.
    TEST_ASSERT_GREATER_THAN_INT32(before, transfers_done.load());
}
