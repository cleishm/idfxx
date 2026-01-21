// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd ili9341
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/ili9341"
#include "unity.h"

#include <type_traits>

using namespace idfxx::lcd;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// ili9341 is non-copyable and non-movable
static_assert(!std::is_copy_constructible_v<ili9341>);
static_assert(!std::is_copy_assignable_v<ili9341>);
static_assert(!std::is_move_constructible_v<ili9341>);
static_assert(!std::is_move_assignable_v<ili9341>);

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

TEST_CASE("ili9341 make with null panel_io returns error", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config{
        .reset_gpio = gpio::nc(),
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    // Passing null panel_io should fail
    std::shared_ptr<idfxx::lcd::panel_io> null_io;
    auto result = ili9341::make(null_io, config);
    TEST_ASSERT_FALSE(result.has_value());
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

TEST_CASE("panel::config flags initialization", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config{
        .reset_gpio = gpio::nc(),
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    // Verify default flags are zero
    TEST_ASSERT_EQUAL(0, config.flags.reset_active_high);
}

TEST_CASE("panel::config with reset_active_high flag", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    panel::config config{
        .reset_gpio = gpio_4,
        .rgb_element_order = rgb_element_order::bgr,
        .bits_per_pixel = 16,
    };

    // Set reset_active_high flag
    config.flags.reset_active_high = 1;

    TEST_ASSERT_EQUAL(1, config.flags.reset_active_high);
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("ili9341 constructor with null panel_io throws", "[idfxx][lcd][ili9341]") {
    using namespace idfxx;

    std::shared_ptr<idfxx::lcd::panel_io> null_io;

    // Constructing with null panel_io should throw - tests exception-based API with inline config
    bool threw = false;
    try {
        std::make_unique<ili9341>(null_io, panel::config{
            .rgb_element_order = rgb_element_order::bgr,
            .bits_per_pixel = 16,
        });
    } catch (const std::system_error&) {
        threw = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}
#endif
