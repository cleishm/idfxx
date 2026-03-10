// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx rotary_encoder
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/rotary_encoder>
#include <unity.h>

#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// rotary_encoder::config is default constructible
static_assert(std::is_default_constructible_v<rotary_encoder::config>);

// rotary_encoder is not copyable
static_assert(!std::is_copy_constructible_v<rotary_encoder>);
static_assert(!std::is_copy_assignable_v<rotary_encoder>);

// rotary_encoder is move-only
static_assert(std::is_move_constructible_v<rotary_encoder>);
static_assert(std::is_move_assignable_v<rotary_encoder>);

// Event type enum values
static_assert(std::to_underlying(rotary_encoder::event_type::changed) == 0);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_released) == 1);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_pressed) == 2);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_long_pressed) == 3);
static_assert(std::to_underlying(rotary_encoder::event_type::btn_clicked) == 4);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// These require a real rotary encoder connected to GPIO pins.
// =============================================================================

TEST_CASE("rotary_encoder::config default values", "[idfxx][rotary_encoder]") {
    rotary_encoder::config cfg{};

    TEST_ASSERT_FALSE(cfg.pin_a.is_connected());
    TEST_ASSERT_FALSE(cfg.pin_b.is_connected());
    TEST_ASSERT_FALSE(cfg.pin_btn.is_connected());
    TEST_ASSERT_EQUAL(gpio::level::low, cfg.btn_active_level);
    TEST_ASSERT_TRUE(cfg.encoder_pins_pull_mode.has_value());
    TEST_ASSERT_EQUAL(gpio::pull_mode::pullup, *cfg.encoder_pins_pull_mode);
    TEST_ASSERT_TRUE(cfg.btn_pin_pull_mode.has_value());
    TEST_ASSERT_EQUAL(gpio::pull_mode::pullup, *cfg.btn_pin_pull_mode);
    TEST_ASSERT_EQUAL(10'000, cfg.btn_dead_time.count());
    TEST_ASSERT_EQUAL(500'000, cfg.btn_long_press_time.count());
    TEST_ASSERT_EQUAL(200, cfg.acceleration_threshold.count());
    TEST_ASSERT_EQUAL(4, cfg.acceleration_cap.count());
    TEST_ASSERT_EQUAL(1'000, cfg.polling_interval.count());
}

TEST_CASE("rotary_encoder::make with no callback fails", "[idfxx][rotary_encoder]") {
    rotary_encoder::config cfg{};
    cfg.pin_a = gpio_4;
    cfg.pin_b = gpio_5;
    auto result = rotary_encoder::make(std::move(cfg));
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), result.error().value());
}

TEST_CASE("rotary_encoder::make with disconnected pin_a fails", "[idfxx][rotary_encoder]") {
    auto result = rotary_encoder::make({
        .pin_a = gpio::nc(),
        .pin_b = gpio_5,
        .callback = [](const rotary_encoder::event&) {},
    });
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), result.error().value());
}

TEST_CASE("rotary_encoder::make with disconnected pin_b fails", "[idfxx][rotary_encoder]") {
    auto result = rotary_encoder::make({
        .pin_a = gpio_4,
        .pin_b = gpio::nc(),
        .callback = [](const rotary_encoder::event&) {},
    });
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), result.error().value());
}

TEST_CASE("rotary_encoder::make succeeds with valid config", "[idfxx][rotary_encoder][hw]") {
    auto result = rotary_encoder::make({
        .pin_a = gpio_4,
        .pin_b = gpio_5,
        .callback = [](const rotary_encoder::event&) {},
    });
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("rotary_encoder enable/disable acceleration", "[idfxx][rotary_encoder][hw]") {
    auto result = rotary_encoder::make({
        .pin_a = gpio_4,
        .pin_b = gpio_5,
        .callback = [](const rotary_encoder::event&) {},
    });
    TEST_ASSERT_TRUE(result.has_value());

    result->enable_acceleration(10);
    result->disable_acceleration();
}

TEST_CASE("rotary_encoder move construction transfers ownership", "[idfxx][rotary_encoder][hw]") {
    auto result = rotary_encoder::make({
        .pin_a = gpio_4,
        .pin_b = gpio_5,
        .callback = [](const rotary_encoder::event&) {},
    });
    TEST_ASSERT_TRUE(result.has_value());

    rotary_encoder moved(std::move(*result));

    // Moved-to object should work
    moved.enable_acceleration(10);
    moved.disable_acceleration();
}

TEST_CASE("rotary_encoder move assignment transfers ownership", "[idfxx][rotary_encoder][hw]") {
    auto r1 = rotary_encoder::make({
        .pin_a = gpio_4,
        .pin_b = gpio_5,
        .callback = [](const rotary_encoder::event&) {},
    });
    TEST_ASSERT_TRUE(r1.has_value());

    auto r2 = rotary_encoder::make({
        .pin_a = gpio_4,
        .pin_b = gpio_5,
        .callback = [](const rotary_encoder::event&) {},
    });
    TEST_ASSERT_TRUE(r2.has_value());

    // Move assignment should clean up target and transfer source
    *r1 = std::move(*r2);

    // r1 should work (has r2's encoder now)
    r1->enable_acceleration(10);
    r1->disable_acceleration();
}

TEST_CASE("rotary_encoder destructor cleanup", "[idfxx][rotary_encoder][hw]") {
    {
        auto result = rotary_encoder::make({
            .pin_a = gpio_4,
            .pin_b = gpio_5,
            .callback = [](const rotary_encoder::event&) {},
        });
        TEST_ASSERT_TRUE(result.has_value());
        // Encoder goes out of scope here and should be cleaned up
    }
    // If the destructor didn't work properly, we'd likely see issues in subsequent tests
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS

TEST_CASE("rotary_encoder constructor succeeds with valid config", "[idfxx][rotary_encoder][hw]") {
    rotary_encoder enc({
        .pin_a = gpio_4,
        .pin_b = gpio_5,
        .callback = [](const rotary_encoder::event&) {},
    });
    // If we get here without an exception, construction succeeded
}

#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
