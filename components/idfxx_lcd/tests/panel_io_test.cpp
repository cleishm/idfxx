// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/color"
#include "idfxx/lcd/panel_io"
#include "unity.h"

#include <esp_lcd_panel_io.h>
#include <type_traits>

using namespace idfxx::lcd;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// spi_config is default-constructible
static_assert(std::is_default_constructible_v<panel_io::spi_config>);

// panel_io is non-copyable
static_assert(!std::is_copy_constructible_v<panel_io>);
static_assert(!std::is_copy_assignable_v<panel_io>);

// panel_io is move-only
static_assert(std::is_move_constructible_v<panel_io>);
static_assert(std::is_move_assignable_v<panel_io>);

// Enum values match ESP-IDF constants
static_assert(std::to_underlying(rgb_element_order::rgb) == LCD_RGB_ELEMENT_ORDER_RGB);
static_assert(std::to_underlying(rgb_element_order::bgr) == LCD_RGB_ELEMENT_ORDER_BGR);

static_assert(std::to_underlying(rgb_data_endian::big) == LCD_RGB_DATA_ENDIAN_BIG);
static_assert(std::to_underlying(rgb_data_endian::little) == LCD_RGB_DATA_ENDIAN_LITTLE);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("spi_config struct initialization", "[idfxx][lcd]") {
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

TEST_CASE("spi_config flags initialization", "[idfxx][lcd]") {
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


TEST_CASE("rgb_element_order enum values", "[idfxx][lcd]") {
    TEST_ASSERT_EQUAL(LCD_RGB_ELEMENT_ORDER_RGB, std::to_underlying(rgb_element_order::rgb));
    TEST_ASSERT_EQUAL(LCD_RGB_ELEMENT_ORDER_BGR, std::to_underlying(rgb_element_order::bgr));
}

TEST_CASE("rgb_data_endian enum values", "[idfxx][lcd]") {
    TEST_ASSERT_EQUAL(LCD_RGB_DATA_ENDIAN_BIG, std::to_underlying(rgb_data_endian::big));
    TEST_ASSERT_EQUAL(LCD_RGB_DATA_ENDIAN_LITTLE, static_cast<int>(rgb_data_endian::little));
}

TEST_CASE("spi_config with custom flags", "[idfxx][lcd]") {
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

TEST_CASE("spi_config with cs pretrans and posttrans", "[idfxx][lcd]") {
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

