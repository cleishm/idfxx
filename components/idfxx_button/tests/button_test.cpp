// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx button
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/button>
#include <unity.h>

#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// button::config is default constructible
static_assert(std::is_default_constructible_v<button::config>);

// button is not copyable
static_assert(!std::is_copy_constructible_v<button>);
static_assert(!std::is_copy_assignable_v<button>);

// button is move-only
static_assert(std::is_move_constructible_v<button>);
static_assert(std::is_move_assignable_v<button>);

// Mode enum values
static_assert(std::to_underlying(button::mode::poll) == 0);
static_assert(std::to_underlying(button::mode::interrupt) == 1);

// Event type enum values
static_assert(std::to_underlying(button::event_type::pressed) == 0);
static_assert(std::to_underlying(button::event_type::released) == 1);
static_assert(std::to_underlying(button::event_type::clicked) == 2);
static_assert(std::to_underlying(button::event_type::long_press) == 3);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("button::config default values", "[idfxx][button]") {
    button::config cfg{};

    TEST_ASSERT_FALSE(cfg.pin.is_connected());
    TEST_ASSERT_EQUAL(button::mode::poll, cfg.mode);
    TEST_ASSERT_EQUAL(gpio::level::low, cfg.pressed_level);
    TEST_ASSERT_TRUE(cfg.enable_pull);
    TEST_ASSERT_FALSE(cfg.autorepeat);
    TEST_ASSERT_EQUAL(50'000, cfg.dead_time.count());
    TEST_ASSERT_EQUAL(1'000'000, cfg.long_press_time.count());
    TEST_ASSERT_EQUAL(500'000, cfg.autorepeat_timeout.count());
    TEST_ASSERT_EQUAL(250'000, cfg.autorepeat_interval.count());
    TEST_ASSERT_EQUAL(10'000, cfg.poll_interval.count());
}

TEST_CASE("button::make with no callback fails", "[idfxx][button]") {
    button::config cfg{};
    cfg.pin = gpio_4;
    auto result = button::make(std::move(cfg));
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), result.error().value());
}

TEST_CASE("button::make with disconnected pin fails", "[idfxx][button]") {
    auto result = button::make({
        .pin = gpio::nc(),
        .callback = [](button::event_type) {},
    });
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), result.error().value());
}

TEST_CASE("button::make succeeds with valid config", "[idfxx][button][hw]") {
    auto result = button::make({
        .pin = gpio_4,
        .callback = [](button::event_type) {},
    });
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("button move construction transfers ownership", "[idfxx][button][hw]") {
    auto result = button::make({
        .pin = gpio_4,
        .callback = [](button::event_type) {},
    });
    TEST_ASSERT_TRUE(result.has_value());

    button moved(std::move(*result));
    // Moved-to object should be valid (destructor runs cleanly)
}

TEST_CASE("button move assignment transfers ownership", "[idfxx][button][hw]") {
    auto r1 = button::make({
        .pin = gpio_4,
        .callback = [](button::event_type) {},
    });
    TEST_ASSERT_TRUE(r1.has_value());

    auto r2 = button::make({
        .pin = gpio_4,
        .callback = [](button::event_type) {},
    });
    TEST_ASSERT_TRUE(r2.has_value());

    // Move assignment should clean up target and transfer source
    *r1 = std::move(*r2);
}

TEST_CASE("button destructor cleanup", "[idfxx][button][hw]") {
    {
        auto result = button::make({
            .pin = gpio_4,
            .callback = [](button::event_type) {},
        });
        TEST_ASSERT_TRUE(result.has_value());
        // Button goes out of scope here and should be cleaned up
    }
    // If the destructor didn't work properly, we'd likely see issues in subsequent tests
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("button constructor succeeds with valid config", "[idfxx][button][hw]") {
    button btn({
        .pin = gpio_4,
        .callback = [](button::event_type) {},
    });
    // If we get here without an exception, construction succeeded
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
