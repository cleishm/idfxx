// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd touch base class
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/touch"
#include "unity.h"

#include <type_traits>

using namespace idfxx::lcd;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// touch is abstract (has pure virtual methods)
static_assert(std::is_abstract_v<touch>);

// touch is non-copyable
static_assert(!std::is_copy_constructible_v<touch>);
static_assert(!std::is_copy_assignable_v<touch>);

// touch has a virtual destructor
static_assert(std::has_virtual_destructor_v<touch>);

// touch is not default-constructible from outside (protected constructor)
static_assert(!std::is_default_constructible_v<touch>);

// process_coordinates_callback is a move_only_function
static_assert(!std::is_copy_constructible_v<touch::process_coordinates_callback>);
static_assert(std::is_move_constructible_v<touch::process_coordinates_callback>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("touch::config struct initialization", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_EQUAL(240, config.x_max);
    TEST_ASSERT_EQUAL(320, config.y_max);
}

TEST_CASE("touch::config default GPIO pins are NC", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_FALSE(config.rst_gpio.is_connected());
    TEST_ASSERT_FALSE(config.int_gpio.is_connected());
}

TEST_CASE("touch::config default flags are zero", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_EQUAL(0, config.flags.swap_xy);
    TEST_ASSERT_EQUAL(0, config.flags.mirror_x);
    TEST_ASSERT_EQUAL(0, config.flags.mirror_y);
}

TEST_CASE("touch::config default levels are zero", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_EQUAL(0, config.levels.reset);
    TEST_ASSERT_EQUAL(0, config.levels.interrupt);
}

TEST_CASE("touch::config process_coordinates defaults to nullptr", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_NULL(config.process_coordinates);
}

TEST_CASE("touch::config with all flags set", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 320,
        .y_max = 240,
        .flags{
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };

    TEST_ASSERT_EQUAL(1, config.flags.swap_xy);
    TEST_ASSERT_EQUAL(1, config.flags.mirror_x);
    TEST_ASSERT_EQUAL(1, config.flags.mirror_y);
}

TEST_CASE("touch::config with GPIO pins", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
        .rst_gpio = gpio_4,
        .int_gpio = gpio_5,
    };

    TEST_ASSERT_TRUE(config.rst_gpio.is_connected());
    TEST_ASSERT_EQUAL(4, config.rst_gpio.num());
    TEST_ASSERT_TRUE(config.int_gpio.is_connected());
    TEST_ASSERT_EQUAL(5, config.int_gpio.num());
}

TEST_CASE("touch::config with process_coordinates callback", "[idfxx][lcd][touch]") {
    using namespace idfxx;

    bool callback_invoked = false;
    touch::config config{
        .x_max = 240,
        .y_max = 320,
        .process_coordinates = [&](uint16_t*, uint16_t*, uint16_t*, uint8_t* point_num, uint8_t) {
            callback_invoked = true;
            *point_num = 0;
        },
    };

    TEST_ASSERT_NOT_NULL(config.process_coordinates);

    // Invoke the callback to verify it works
    uint16_t x = 100, y = 200, strength = 50;
    uint8_t point_num = 1;
    config.process_coordinates(&x, &y, &strength, &point_num, 5);
    TEST_ASSERT_TRUE(callback_invoked);
    TEST_ASSERT_EQUAL(0, point_num);
}
