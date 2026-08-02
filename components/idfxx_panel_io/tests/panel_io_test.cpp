// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx panel_io
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/panel_io"
#include "unity.h"

#include <esp_lcd_panel_io.h>
#include <type_traits>

using idfxx::panel_io;
using namespace frequency_literals;

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

// spi_config is default-constructible
static_assert(std::is_default_constructible_v<panel_io::spi_config>);

// i2c_config is default-constructible
static_assert(std::is_default_constructible_v<panel_io::i2c_config>);

// panel_io is non-copyable
static_assert(!std::is_copy_constructible_v<panel_io>);
static_assert(!std::is_copy_assignable_v<panel_io>);

// panel_io is move-only
static_assert(std::is_move_constructible_v<panel_io>);
static_assert(std::is_move_assignable_v<panel_io>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("spi_config struct initialization", "[idfxx][panel_io]") {
    using namespace idfxx;

    panel_io::spi_config config{
        .cs_gpio = gpio_5,
        .dc_gpio = gpio_2,
        .spi_mode = 0,
        .pclk_freq = freq::megahertz(10),
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    TEST_ASSERT_EQUAL(5, config.cs_gpio.num());
    TEST_ASSERT_EQUAL(2, config.dc_gpio.num());
    TEST_ASSERT_EQUAL(0, config.spi_mode);
    TEST_ASSERT_EQUAL(10000000, config.pclk_freq.count());
    TEST_ASSERT_EQUAL(10, config.trans_queue_depth);
    TEST_ASSERT_EQUAL(8, config.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(8, config.lcd_param_bits);
}

TEST_CASE("spi_config flags initialization", "[idfxx][panel_io]") {
    panel_io::spi_config config{
        .cs_gpio = idfxx::gpio_5,
        .dc_gpio = idfxx::gpio_2,
        .spi_mode = 0,
        .pclk_freq = freq::megahertz(10),
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    // Verify default flags are zero
    TEST_ASSERT_EQUAL(0, config.flags.dc_high_on_cmd);
    TEST_ASSERT_EQUAL(0, config.flags.dc_low_on_data);
    TEST_ASSERT_EQUAL(0, config.flags.dc_low_on_param);
    TEST_ASSERT_EQUAL(0, config.flags.octal_mode);
    TEST_ASSERT_EQUAL(0, config.flags.quad_mode);
    TEST_ASSERT_EQUAL(0, config.flags.sio_mode);
    TEST_ASSERT_EQUAL(0, config.flags.lsb_first);
    TEST_ASSERT_EQUAL(0, config.flags.cs_high_active);
}

TEST_CASE("spi_config with custom flags", "[idfxx][panel_io]") {
    using namespace idfxx;

    panel_io::spi_config config{
        .cs_gpio = gpio_5,
        .dc_gpio = gpio_2,
        .spi_mode = 0,
        .pclk_freq = freq::megahertz(40),
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    // Set some flags
    config.flags.dc_low_on_data = 1;
    config.flags.lsb_first = 1;

    TEST_ASSERT_EQUAL(1, config.flags.dc_low_on_data);
    TEST_ASSERT_EQUAL(1, config.flags.lsb_first);
    TEST_ASSERT_EQUAL(0, config.flags.dc_high_on_cmd);
}

TEST_CASE("spi_config with cs pretrans and posttrans", "[idfxx][panel_io]") {
    using namespace idfxx;

    panel_io::spi_config config{
        .cs_gpio = gpio_5,
        .dc_gpio = gpio_2,
        .spi_mode = 0,
        .pclk_freq = freq::megahertz(10),
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .cs_enable_pretrans = 4,
        .cs_enable_posttrans = 8,
    };

    TEST_ASSERT_EQUAL(4, config.cs_enable_pretrans);
    TEST_ASSERT_EQUAL(8, config.cs_enable_posttrans);
}

TEST_CASE("i2c_config default initialization", "[idfxx][panel_io]") {
    panel_io::i2c_config config{};

    TEST_ASSERT_EQUAL(0, config.device_address);
    TEST_ASSERT_EQUAL(0, config.scl_speed.count());
    TEST_ASSERT_EQUAL(0, config.control_phase_bytes);
    TEST_ASSERT_EQUAL(0, config.dc_bit_offset);
    TEST_ASSERT_EQUAL(0, config.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(0, config.lcd_param_bits);
    TEST_ASSERT_EQUAL(0, config.flags.dc_low_on_data);
    TEST_ASSERT_EQUAL(0, config.flags.disable_control_phase);
}

TEST_CASE("i2c_config struct initialization", "[idfxx][panel_io]") {
    panel_io::i2c_config config{
        .device_address = 0x3C,
        .scl_speed = 400_kHz,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    TEST_ASSERT_EQUAL(0x3C, config.device_address);
    TEST_ASSERT_EQUAL(400000, config.scl_speed.count());
    TEST_ASSERT_EQUAL(1, config.control_phase_bytes);
    TEST_ASSERT_EQUAL(6, config.dc_bit_offset);
    TEST_ASSERT_EQUAL(8, config.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(8, config.lcd_param_bits);
}

TEST_CASE("i2c panel_io make rejects invalid config", "[idfxx][panel_io]") {
    auto bus = idfxx::i2c::master_bus::make(idfxx::i2c::port::i2c0, TEST_SDA, TEST_SCL, 400_kHz);
    TEST_ASSERT_TRUE(bus.has_value());

    // Default config is missing device_address, scl_speed, and bit widths.
    auto io = panel_io::make(*bus, {});
    TEST_ASSERT_FALSE(io.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), io.error().value());

    // Missing scl_speed.
    io = panel_io::make(*bus, {.device_address = 0x3C, .lcd_cmd_bits = 8, .lcd_param_bits = 8});
    TEST_ASSERT_FALSE(io.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), io.error().value());

    // Missing lcd_cmd_bits / lcd_param_bits.
    io = panel_io::make(*bus, {.device_address = 0x3C, .scl_speed = 400_kHz});
    TEST_ASSERT_FALSE(io.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), io.error().value());
}
