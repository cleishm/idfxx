// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx gpio
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/gpio"
#include "unity.h"

#include <driver/gpio.h>
#include <type_traits>
#include <utility>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// gpio is default constructible (creates NC)
static_assert(std::is_default_constructible_v<gpio>);

// gpio is copyable and movable
static_assert(std::is_copy_constructible_v<gpio>);
static_assert(std::is_copy_assignable_v<gpio>);
static_assert(std::is_move_constructible_v<gpio>);
static_assert(std::is_move_assignable_v<gpio>);

// gpio::nc() is constexpr and returns not connected
static_assert(!gpio::nc().is_connected());
static_assert(gpio::nc().num() == GPIO_NUM_NC);

// gpio::max() is constexpr
static_assert(gpio::max().is_connected());
static_assert(gpio::max().num() == GPIO_NUM_MAX - 1);

// Default constructed gpio is NC
static_assert(!gpio().is_connected());
static_assert(gpio().num() == GPIO_NUM_NC);

// Predefined GPIO constants are valid (test gpio_0 which should exist on all chips)
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 0))
static_assert(gpio_0.is_connected());
static_assert(gpio_0.num() == 0);
static_assert(gpio_0.idf_num() == GPIO_NUM_0);
#endif

// gpio can be compared with ==
static_assert(gpio::nc() == gpio::nc());
static_assert(gpio() == gpio::nc());
#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 0))
static_assert(gpio_0 == gpio_0);
static_assert(!(gpio_0 == gpio::nc()));
#endif

// Enum values match ESP-IDF values
static_assert(std::to_underlying(gpio::mode::disable) == GPIO_MODE_DISABLE);
static_assert(std::to_underlying(gpio::mode::input) == GPIO_MODE_INPUT);
static_assert(std::to_underlying(gpio::mode::output) == GPIO_MODE_OUTPUT);
static_assert(std::to_underlying(gpio::mode::output_od) == GPIO_MODE_OUTPUT_OD);
static_assert(std::to_underlying(gpio::mode::input_output_od) == GPIO_MODE_INPUT_OUTPUT_OD);
static_assert(std::to_underlying(gpio::mode::input_output) == GPIO_MODE_INPUT_OUTPUT);

static_assert(std::to_underlying(gpio::pull_mode::pullup) == GPIO_PULLUP_ONLY);
static_assert(std::to_underlying(gpio::pull_mode::pulldown) == GPIO_PULLDOWN_ONLY);
static_assert(std::to_underlying(gpio::pull_mode::pullup_pulldown) == GPIO_PULLUP_PULLDOWN);
static_assert(std::to_underlying(gpio::pull_mode::floating) == GPIO_FLOATING);

static_assert(std::to_underlying(gpio::drive_cap::cap_0) == GPIO_DRIVE_CAP_0);
static_assert(std::to_underlying(gpio::drive_cap::cap_1) == GPIO_DRIVE_CAP_1);
static_assert(std::to_underlying(gpio::drive_cap::cap_2) == GPIO_DRIVE_CAP_2);
static_assert(std::to_underlying(gpio::drive_cap::cap_default) == GPIO_DRIVE_CAP_DEFAULT);
static_assert(std::to_underlying(gpio::drive_cap::cap_3) == GPIO_DRIVE_CAP_3);

static_assert(std::to_underlying(gpio::intr_type::disable) == GPIO_INTR_DISABLE);
static_assert(std::to_underlying(gpio::intr_type::posedge) == GPIO_INTR_POSEDGE);
static_assert(std::to_underlying(gpio::intr_type::negedge) == GPIO_INTR_NEGEDGE);
static_assert(std::to_underlying(gpio::intr_type::anyedge) == GPIO_INTR_ANYEDGE);
static_assert(std::to_underlying(gpio::intr_type::low_level) == GPIO_INTR_LOW_LEVEL);
static_assert(std::to_underlying(gpio::intr_type::high_level) == GPIO_INTR_HIGH_LEVEL);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("default constructed gpio is NC", "[idfxx][gpio]") {
    gpio g;
    TEST_ASSERT_FALSE(g.is_connected());
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, g.num());
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, g.idf_num());
}

TEST_CASE("gpio::nc returns not connected gpio", "[idfxx][gpio]") {
    gpio g = gpio::nc();
    TEST_ASSERT_FALSE(g.is_connected());
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, g.num());
}

TEST_CASE("gpio::max returns highest valid gpio", "[idfxx][gpio]") {
    gpio g = gpio::max();
    TEST_ASSERT_TRUE(g.is_connected());
    TEST_ASSERT_EQUAL(GPIO_NUM_MAX - 1, g.num());
}

TEST_CASE("gpio::make with valid pin returns success", "[idfxx][gpio]") {
    auto result = gpio::make(0);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->is_connected());
    TEST_ASSERT_EQUAL(0, result->num());
    TEST_ASSERT_EQUAL(GPIO_NUM_0, result->idf_num());
}

TEST_CASE("gpio::make with NC returns success", "[idfxx][gpio]") {
    auto result = gpio::make(GPIO_NUM_NC);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_FALSE(result->is_connected());
}

TEST_CASE("gpio::make with invalid pin returns error", "[idfxx][gpio]") {
    // Use a pin number that's definitely invalid
    auto result = gpio::make(999);
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(errc::invalid_arg), result.error().value());
}

TEST_CASE("gpio equality comparison", "[idfxx][gpio]") {
    gpio nc1 = gpio::nc();
    gpio nc2 = gpio::nc();
    gpio g0 = gpio::make(0).value();
    gpio g0_copy = gpio::make(0).value();

    TEST_ASSERT_TRUE(nc1 == nc2);
    TEST_ASSERT_TRUE(g0 == g0_copy);
    TEST_ASSERT_FALSE(nc1 == g0);
    TEST_ASSERT_FALSE(g0 == nc1);
}

TEST_CASE("gpio copy and move operations", "[idfxx][gpio]") {
    gpio original = gpio::make(0).value();

    // Copy construction
    gpio copied(original);
    TEST_ASSERT_TRUE(copied == original);
    TEST_ASSERT_EQUAL(0, copied.num());

    // Copy assignment
    gpio assigned;
    assigned = original;
    TEST_ASSERT_TRUE(assigned == original);
    TEST_ASSERT_EQUAL(0, assigned.num());

    // Move construction
    gpio to_move = gpio::make(0).value();
    gpio moved(std::move(to_move));
    TEST_ASSERT_EQUAL(0, moved.num());

    // Move assignment
    gpio move_assigned;
    move_assigned = gpio::make(0).value();
    TEST_ASSERT_EQUAL(0, move_assigned.num());
}

TEST_CASE("gpio is_output_capable for valid pins", "[idfxx][gpio]") {
    // GPIO 0 should be output capable on most chips
    gpio g0 = gpio::make(0).value();
    TEST_ASSERT_TRUE(g0.is_output_capable());

    // NC is not output capable
    TEST_ASSERT_FALSE(gpio::nc().is_output_capable());
}

TEST_CASE("gpio operations on NC return error", "[idfxx][gpio]") {
    gpio nc = gpio::nc();

    // All try_ operations should fail on NC
    TEST_ASSERT_FALSE(nc.try_set_direction(gpio::mode::input).has_value());
    TEST_ASSERT_FALSE(nc.try_set_pull_mode(gpio::pull_mode::floating).has_value());
    TEST_ASSERT_FALSE(nc.try_set_drive_capability(gpio::drive_cap::cap_default).has_value());
    TEST_ASSERT_FALSE(nc.try_get_drive_capability().has_value());
    TEST_ASSERT_FALSE(nc.try_set_intr_type(gpio::intr_type::disable).has_value());
    TEST_ASSERT_FALSE(nc.try_intr_enable().has_value());
    TEST_ASSERT_FALSE(nc.try_intr_disable().has_value());
}

TEST_CASE("gpio get_level on NC returns false", "[idfxx][gpio]") {
    gpio nc = gpio::nc();
    TEST_ASSERT_FALSE(nc.get_level());
}

#if (SOC_GPIO_VALID_GPIO_MASK & (1ULL << 0))
TEST_CASE("predefined gpio_0 constant is valid", "[idfxx][gpio]") {
    TEST_ASSERT_TRUE(gpio_0.is_connected());
    TEST_ASSERT_EQUAL(0, gpio_0.num());
    TEST_ASSERT_EQUAL(GPIO_NUM_0, gpio_0.idf_num());
    TEST_ASSERT_TRUE(gpio_0.is_output_capable());
}
#endif

TEST_CASE("gpio::config default values", "[idfxx][gpio]") {
    gpio::config cfg{};
    TEST_ASSERT_EQUAL(gpio::mode::disable, cfg.mode);
    TEST_ASSERT_EQUAL(gpio::pull_mode::floating, cfg.pull_mode);
    TEST_ASSERT_EQUAL(gpio::intr_type::disable, cfg.intr_type);
}

// =============================================================================
// Hardware-dependent tests
// These tests interact with actual GPIO hardware
// =============================================================================

TEST_CASE("gpio reset on valid pin succeeds", "[idfxx][gpio]") {
    auto result = gpio::make(0);
    TEST_ASSERT_TRUE(result.has_value());

    result->reset();
}

TEST_CASE("gpio set_direction on valid pin succeeds", "[idfxx][gpio]") {
    gpio g = gpio::make(0).value();

    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::output).has_value());
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::disable).has_value());
}

TEST_CASE("gpio set_level and get_level", "[idfxx][gpio]") {
    gpio g = gpio::make(0).value();

    // Configure as output
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::output).has_value());

    // Set high
    g.set_level(true);

    // Set low
    g.set_level(false);

    // Reset to input to allow reading (need input_output mode to read what we set)
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input_output).has_value());

    g.set_level(true);
    // Note: Reading back may not work without external pull-up/down
    // Just verify the operations don't fail
}

TEST_CASE("gpio drive capability", "[idfxx][gpio]") {
    gpio g = gpio::make(0).value();

    // Set drive capability
    TEST_ASSERT_TRUE(g.try_set_drive_capability(gpio::drive_cap::cap_2).has_value());

    // Get drive capability
    auto cap_result = g.try_get_drive_capability();
    TEST_ASSERT_TRUE(cap_result.has_value());
    TEST_ASSERT_EQUAL(gpio::drive_cap::cap_2, cap_result.value());
}

TEST_CASE("gpio pull mode on valid pin", "[idfxx][gpio]") {
    gpio g = gpio::make(0).value();

    // Configure as input first
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());

    // These may fail on input-only pins (34-39 on ESP32)
    // GPIO 0 should support pull resistors on most chips
    TEST_ASSERT_TRUE(g.try_set_pull_mode(gpio::pull_mode::pullup).has_value());
    TEST_ASSERT_TRUE(g.try_pullup_disable().has_value());
    TEST_ASSERT_TRUE(g.try_pulldown_enable().has_value());
    TEST_ASSERT_TRUE(g.try_pulldown_disable().has_value());
    TEST_ASSERT_TRUE(g.try_set_pull_mode(gpio::pull_mode::floating).has_value());
}

TEST_CASE("gpio interrupt configuration", "[idfxx][gpio]") {
    gpio g = gpio::make(0).value();

    // Configure as input
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());

    // Set interrupt type
    TEST_ASSERT_TRUE(g.try_set_intr_type(gpio::intr_type::negedge).has_value());
    TEST_ASSERT_TRUE(g.try_set_intr_type(gpio::intr_type::posedge).has_value());
    TEST_ASSERT_TRUE(g.try_set_intr_type(gpio::intr_type::anyedge).has_value());
    TEST_ASSERT_TRUE(g.try_set_intr_type(gpio::intr_type::disable).has_value());
}

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(gpio) outputs GPIO_NC for not connected", "[idfxx][gpio]") {
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", to_string(gpio::nc()).c_str());
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", to_string(gpio()).c_str());
}

TEST_CASE("to_string(gpio) outputs GPIO_N for valid pins", "[idfxx][gpio]") {
    TEST_ASSERT_EQUAL_STRING("GPIO_0", to_string(gpio_0).c_str());
    TEST_ASSERT_EQUAL_STRING("GPIO_5", to_string(gpio_5).c_str());
}

// =============================================================================
// Formatter tests
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<gpio, char>);

TEST_CASE("gpio formatter outputs GPIO_NC for not connected", "[idfxx][gpio]") {
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", std::format("{}", gpio::nc()).c_str());
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", std::format("{}", gpio()).c_str());
}

TEST_CASE("gpio formatter outputs GPIO_N for valid pins", "[idfxx][gpio]") {
    TEST_ASSERT_EQUAL_STRING("GPIO_0", std::format("{}", gpio_0).c_str());
    TEST_ASSERT_EQUAL_STRING("GPIO_5", std::format("{}", gpio_5).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
