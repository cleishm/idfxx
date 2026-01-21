// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx lcd stmpe610
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/lcd/stmpe610"
#include "unity.h"

#include <type_traits>

using namespace idfxx::lcd;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// stmpe610 is non-copyable and non-movable
static_assert(!std::is_copy_constructible_v<stmpe610>);
static_assert(!std::is_copy_assignable_v<stmpe610>);
static_assert(!std::is_move_constructible_v<stmpe610>);
static_assert(!std::is_move_assignable_v<stmpe610>);

// stmpe610 inherits from touch base class
static_assert(std::is_base_of_v<touch, stmpe610>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("touch::config struct initialization", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_EQUAL(240, config.x_max);
    TEST_ASSERT_EQUAL(320, config.y_max);
}

TEST_CASE("touch::config default rst_gpio and int_gpio are NC", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_FALSE(config.rst_gpio.is_connected());
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.rst_gpio.num());
    TEST_ASSERT_FALSE(config.int_gpio.is_connected());
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, config.int_gpio.num());
}

TEST_CASE("stmpe610 make with null panel_io returns error", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    // Passing null panel_io should fail - use inline config
    std::shared_ptr<idfxx::lcd::panel_io> null_io;
    auto result = stmpe610::make(null_io, {
        .x_max = 240,
        .y_max = 320,
    });
    TEST_ASSERT_FALSE(result.has_value());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("stmpe610 constructor with null panel_io throws", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    std::shared_ptr<idfxx::lcd::panel_io> null_io;

    // Constructing with null panel_io should throw - tests exception-based API with inline config
    bool threw = false;
    try {
        std::make_unique<stmpe610>(null_io, touch::config{
            .x_max = 240,
            .y_max = 320,
        });
    } catch (const std::system_error&) {
        threw = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}
#endif

TEST_CASE("touch::config with different x_max and y_max values", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config_portrait{
        .x_max = 240,
        .y_max = 320,
    };

    touch::config config_landscape{
        .x_max = 320,
        .y_max = 240,
    };

    TEST_ASSERT_EQUAL(240, config_portrait.x_max);
    TEST_ASSERT_EQUAL(320, config_portrait.y_max);
    TEST_ASSERT_EQUAL(320, config_landscape.x_max);
    TEST_ASSERT_EQUAL(240, config_landscape.y_max);
}

TEST_CASE("touch::config orientation flags initialization", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    // Verify default flags are zero
    TEST_ASSERT_EQUAL(0, config.flags.swap_xy);
    TEST_ASSERT_EQUAL(0, config.flags.mirror_x);
    TEST_ASSERT_EQUAL(0, config.flags.mirror_y);
}

TEST_CASE("touch::config with portrait orientation flags", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
        .flags{
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    TEST_ASSERT_EQUAL(0, config.flags.swap_xy);
    TEST_ASSERT_EQUAL(0, config.flags.mirror_x);
    TEST_ASSERT_EQUAL(0, config.flags.mirror_y);
}

TEST_CASE("touch::config with landscape orientation flags", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 320,
        .y_max = 240,
        .flags{
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    TEST_ASSERT_EQUAL(1, config.flags.swap_xy);
    TEST_ASSERT_EQUAL(1, config.flags.mirror_x);
    TEST_ASSERT_EQUAL(0, config.flags.mirror_y);
}

TEST_CASE("touch::config with reset and interrupt GPIO pins", "[idfxx][lcd][stmpe610]") {
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

TEST_CASE("touch::config levels initialization", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    // Verify default levels are zero
    TEST_ASSERT_EQUAL(0, config.levels.reset);
    TEST_ASSERT_EQUAL(0, config.levels.interrupt);
}

TEST_CASE("touch::config with custom levels", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
        .levels{
            .reset = 1,
            .interrupt = 1,
        },
    };

    TEST_ASSERT_EQUAL(1, config.levels.reset);
    TEST_ASSERT_EQUAL(1, config.levels.interrupt);
}

TEST_CASE("touch::config process_coordinates defaults to nullptr", "[idfxx][lcd][stmpe610]") {
    using namespace idfxx;

    touch::config config{
        .x_max = 240,
        .y_max = 320,
    };

    TEST_ASSERT_NULL(config.process_coordinates);
}
