// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd ssd1306
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/mono_framebuffer"
#include "idfxx/lcd/ssd1306"
#include "unity.h"

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

using namespace idfxx::lcd;
using namespace quanta::frequency_literals;

// SDA/SCL pins used by the I2C bus-creation tests. Any two free pins work; the
// ESP32-C2 only has GPIO 0-20, so it uses a lower SDA pin than the default 21.
#ifdef CONFIG_IDF_TARGET_ESP32C2
inline constexpr auto TEST_SDA = idfxx::gpio_4;
#else
inline constexpr auto TEST_SDA = idfxx::gpio_21;
#endif
inline constexpr auto TEST_SCL = idfxx::gpio_9;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// ssd1306::config is default-constructible
static_assert(std::is_default_constructible_v<ssd1306::config>);

// ssd1306 is non-copyable
static_assert(!std::is_copy_constructible_v<ssd1306>);
static_assert(!std::is_copy_assignable_v<ssd1306>);

// ssd1306 is move-only
static_assert(std::is_move_constructible_v<ssd1306>);
static_assert(std::is_move_assignable_v<ssd1306>);

// ssd1306 inherits from panel base class
static_assert(std::is_base_of_v<panel, ssd1306>);

// try_set_contrast follows the result-based API convention
static_assert(std::is_same_v<decltype(std::declval<ssd1306&>().try_set_contrast(uint8_t{})), idfxx::result<void>>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("ssd1306::config defaults", "[idfxx][lcd][ssd1306]") {
    using namespace idfxx;

    ssd1306::config config{};

    TEST_ASSERT_FALSE(config.reset_gpio.is_connected());
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.reset_gpio.num());
    TEST_ASSERT_EQUAL(64, config.height);
    TEST_ASSERT_EQUAL(gpio::level::low, config.reset_active_level);
    TEST_ASSERT_EQUAL(128, config.contrast);
}

TEST_CASE("ssd1306::config struct initialization", "[idfxx][lcd][ssd1306]") {
    using namespace idfxx;

    ssd1306::config config{
        .reset_gpio = gpio_4,
        .height = 32,
        .reset_active_level = gpio::level::high,
    };

    TEST_ASSERT_EQUAL(4, config.reset_gpio.num());
    TEST_ASSERT_EQUAL(32, config.height);
    TEST_ASSERT_EQUAL(gpio::level::high, config.reset_active_level);
}

TEST_CASE("ssd1306::config contrast initialization", "[idfxx][lcd][ssd1306]") {
    ssd1306::config config{.contrast = 0};

    TEST_ASSERT_EQUAL(0, config.contrast);
}

TEST_CASE("ssd1306::i2c_io_config supplies the controller framing", "[idfxx][lcd][ssd1306]") {
    auto config = ssd1306::i2c_io_config();

    TEST_ASSERT_EQUAL(0x3C, *config.device_address);
    TEST_ASSERT_EQUAL(400000, config.scl_speed->count());
    TEST_ASSERT_EQUAL(1, config.control_phase_bytes);
    TEST_ASSERT_EQUAL(6, config.dc_bit_offset);
    TEST_ASSERT_EQUAL(8, *config.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(8, *config.lcd_param_bits);

    config = ssd1306::i2c_io_config(0x3D, 100_kHz);
    TEST_ASSERT_EQUAL(0x3D, *config.device_address);
    TEST_ASSERT_EQUAL(100000, config.scl_speed->count());
}

TEST_CASE("ssd1306::make rejects invalid height", "[idfxx][lcd][ssd1306]") {
    auto bus = idfxx::i2c::master_bus::make(idfxx::i2c::port::i2c0, TEST_SDA, TEST_SCL, 400_kHz);
    TEST_ASSERT_TRUE(bus.has_value());

    auto io = idfxx::panel_io::make(*bus, ssd1306::i2c_io_config());
    TEST_ASSERT_TRUE(io.has_value());

    // Height validation fails before any bus traffic, so no device needs to be attached.
    auto display = ssd1306::make(*io, {.height = 48});
    TEST_ASSERT_FALSE(display.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), display.error().value());
}

// Drives a real SPI bus, so it runs on hardware only.
TEST_CASE("ssd1306 draw_bitmap requires page-aligned regions", "[idfxx][lcd][ssd1306][hw]") {
    // Commands get no reply over SPI, so the panel is created without one attached. The pins
    // match the panel_io SPI tests; there is no D/C line.
    idfxx::spi::bus_config bus_cfg{};
    bus_cfg.mosi = idfxx::gpio_11;
    bus_cfg.sclk = idfxx::gpio_12;
    auto bus = idfxx::spi::master_bus::make(idfxx::spi::host_device::spi2, idfxx::spi::dma_chan::disabled, bus_cfg);
    TEST_ASSERT_TRUE(bus.has_value());

    auto io = idfxx::panel_io::make(*bus, idfxx::panel_io::spi_config{
        .cs_gpio = idfxx::gpio_10,
        .pclk_freq = 1_MHz,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    });
    TEST_ASSERT_TRUE(io.has_value());

    auto display = ssd1306::make(*io, {});
    TEST_ASSERT_TRUE(display.has_value());
    TEST_ASSERT_FALSE(display->axes_swapped());

    std::array<uint8_t, 16> data{};
    auto assert_invalid_arg = [](const idfxx::result<void>& r) {
        TEST_ASSERT_FALSE(r.has_value());
        TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), r.error().value());
    };
    auto fb = mono_framebuffer::make(16, 8);
    TEST_ASSERT_TRUE(fb.has_value());

    // Unswapped, the pages run along y: 16 columns of rows [4, 12), or of rows [0, 4).
    assert_invalid_arg(display->try_draw_bitmap(0, 4, 16, 12, data));
    assert_invalid_arg(display->try_draw_bitmap(0, 0, 16, 4, idfxx::byte_view{data}.first(8)));

    // A page-aligned region passes, whatever its columns, as does a framebuffer.
    TEST_ASSERT_TRUE(display->try_draw_bitmap(0, 8, 16, 16, data).has_value());
    TEST_ASSERT_TRUE(display->try_draw_bitmap(3, 0, 5, 32, idfxx::byte_view{data}.first(8)).has_value());
    TEST_ASSERT_TRUE(display->try_write(*fb).has_value());

    // Swapped, they run along x instead.
    TEST_ASSERT_TRUE(display->try_swap_xy(true).has_value());
    TEST_ASSERT_TRUE(display->axes_swapped());
    assert_invalid_arg(display->try_draw_bitmap(4, 0, 12, 16, data));
    assert_invalid_arg(display->try_draw_bitmap(0, 0, 4, 16, idfxx::byte_view{data}.first(8)));

    // A region aligned along x passes, whatever its rows.
    TEST_ASSERT_TRUE(display->try_draw_bitmap(0, 4, 8, 12, idfxx::byte_view{data}.first(8)).has_value());
    TEST_ASSERT_TRUE(display->try_draw_bitmap(8, 3, 16, 5, idfxx::byte_view{data}.first(2)).has_value());

    // A framebuffer's pages group rows, which the swapped panel can't take.
    auto written = display->try_write(*fb);
    TEST_ASSERT_FALSE(written.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), written.error().value());

    // Unswapped again, the framebuffer and the row-aligned regions pass once more.
    TEST_ASSERT_TRUE(display->try_swap_xy(false).has_value());
    TEST_ASSERT_FALSE(display->axes_swapped());
    TEST_ASSERT_TRUE(display->try_write(*fb).has_value());
    assert_invalid_arg(display->try_draw_bitmap(0, 4, 8, 12, idfxx::byte_view{data}.first(8)));
}
