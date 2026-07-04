// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd ssd1306
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/ssd1306"
#include "unity.h"

#include <type_traits>
#include <utility>

using namespace idfxx::lcd;
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

TEST_CASE("ssd1306::i2c_io_config supplies the controller framing", "[idfxx][lcd][ssd1306]") {
    auto config = ssd1306::i2c_io_config();

    TEST_ASSERT_EQUAL(0x3C, config.device_address);
    TEST_ASSERT_EQUAL(400000, config.scl_speed.count());
    TEST_ASSERT_EQUAL(1, config.control_phase_bytes);
    TEST_ASSERT_EQUAL(6, config.dc_bit_offset);
    TEST_ASSERT_EQUAL(8, config.lcd_cmd_bits);
    TEST_ASSERT_EQUAL(8, config.lcd_param_bits);

    config = ssd1306::i2c_io_config(0x3D, 100_kHz);
    TEST_ASSERT_EQUAL(0x3D, config.device_address);
    TEST_ASSERT_EQUAL(100000, config.scl_speed.count());
}

TEST_CASE("ssd1306::make rejects invalid height", "[idfxx][lcd][ssd1306]") {
    auto bus = idfxx::i2c::master_bus::make(idfxx::i2c::port::i2c0, TEST_SDA, TEST_SCL, 400_kHz);
    TEST_ASSERT_TRUE(bus.has_value());

    auto io = panel_io::make(*bus, ssd1306::i2c_io_config());
    TEST_ASSERT_TRUE(io.has_value());

    // Height validation fails before any bus traffic, so no device needs to be attached.
    auto display = ssd1306::make(*io, {.height = 48});
    TEST_ASSERT_FALSE(display.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), display.error().value());
}
