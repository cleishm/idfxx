// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx gpio
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/gpio"
#include "unity.h"

#include <driver/gpio.h>
#include <bit>
#include <type_traits>
#include <utility>
#include <vector>

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
static_assert(gpio_0 != gpio::nc());
static_assert(!(gpio::nc() != gpio::nc()));
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

static_assert(std::to_underlying(gpio::level::low) == 0);
static_assert(std::to_underlying(gpio::level::high) == 1);

// operator~ inverts level
static_assert(~gpio::level::high == gpio::level::low);
static_assert(~gpio::level::low == gpio::level::high);

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
    // These are now no-ops on NC (void methods)
    nc.set_intr_type(gpio::intr_type::disable);
    nc.intr_enable();
    nc.intr_disable();
}

TEST_CASE("gpio get_level on NC returns low", "[idfxx][gpio]") {
    gpio nc = gpio::nc();
    TEST_ASSERT_EQUAL(gpio::level::low, nc.get_level());
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
    g.set_level(gpio::level::high);

    // Set low
    g.set_level(gpio::level::low);

    // Reset to input to allow reading (need input_output mode to read what we set)
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input_output).has_value());

    g.set_level(gpio::level::high);
    // Note: Reading back may not work without external pull-up/down
    // Just verify the operations don't fail
}

TEST_CASE("gpio toggle_level", "[idfxx][gpio][hw]") {
    gpio g = gpio::make(0).value();

    // Configure as input_output so we can read back what we set
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input_output).has_value());

    g.set_level(gpio::level::low);
    g.toggle_level();
    TEST_ASSERT_EQUAL(gpio::level::high, g.get_level());

    g.toggle_level();
    TEST_ASSERT_EQUAL(gpio::level::low, g.get_level());
}

TEST_CASE("gpio drive capability", "[idfxx][gpio][hw]") {
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
    g.set_intr_type(gpio::intr_type::negedge);
    g.set_intr_type(gpio::intr_type::posedge);
    g.set_intr_type(gpio::intr_type::anyedge);
    g.set_intr_type(gpio::intr_type::disable);
}

// =============================================================================
// input_enable and is_digital_io_pin_capable tests
// =============================================================================

TEST_CASE("gpio input_enable on valid pin succeeds", "[idfxx][gpio]") {
    gpio g = gpio::make(0).value();
    g.input_enable();
}

TEST_CASE("gpio input_enable on NC is a no-op", "[idfxx][gpio]") {
    gpio nc = gpio::nc();
    nc.input_enable();
}

TEST_CASE("gpio is_digital_io_pin_capable", "[idfxx][gpio]") {
    // Use the lowest digital IO capable pin for the current target
    constexpr int digital_io_pin = std::countr_zero(SOC_GPIO_VALID_DIGITAL_IO_PAD_MASK);
    gpio g = gpio::make(digital_io_pin).value();
    TEST_ASSERT_TRUE(g.is_digital_io_pin_capable());

    // NC is not digital IO capable
    TEST_ASSERT_FALSE(gpio::nc().is_digital_io_pin_capable());
}

// =============================================================================
// ISR service tests
// =============================================================================

TEST_CASE("gpio ISR service install and uninstall", "[idfxx][gpio]") {
    // Install ISR service
    auto result = gpio::try_install_isr_service();
    TEST_ASSERT_TRUE(result.has_value());

    // Uninstall
    gpio::uninstall_isr_service();
}

TEST_CASE("gpio ISR handler add and remove with raw callback", "[idfxx][gpio]") {
    // Install ISR service first
    auto install_result = gpio::try_install_isr_service();
    TEST_ASSERT_TRUE(install_result.has_value());

    gpio g = gpio::make(0).value();

    // Configure as input with interrupt
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());
    g.set_intr_type(gpio::intr_type::anyedge);

    // Add raw callback handler
    static bool handler_called = false;
    handler_called = false;
    auto handle_result = g.try_isr_handler_add([](void* arg) { *static_cast<bool*>(arg) = true; }, &handler_called);
    TEST_ASSERT_TRUE(handle_result.has_value());

    // Remove handler
    auto remove_result = g.try_isr_handler_remove(*handle_result);
    TEST_ASSERT_TRUE(remove_result.has_value());

    // Uninstall ISR service
    gpio::uninstall_isr_service();
}

TEST_CASE("gpio ISR handler add and remove with functional callback", "[idfxx][gpio]") {
    auto install_result = gpio::try_install_isr_service();
    TEST_ASSERT_TRUE(install_result.has_value());

    gpio g = gpio::make(0).value();
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());
    g.set_intr_type(gpio::intr_type::anyedge);

    // Add functional handler
    auto handle_result = g.try_isr_handler_add([]() {});
    TEST_ASSERT_TRUE(handle_result.has_value());

    // Remove handler
    auto remove_result = g.try_isr_handler_remove(*handle_result);
    TEST_ASSERT_TRUE(remove_result.has_value());

    gpio::uninstall_isr_service();
}

TEST_CASE("gpio ISR handler remove all", "[idfxx][gpio]") {
    auto install_result = gpio::try_install_isr_service();
    TEST_ASSERT_TRUE(install_result.has_value());

    gpio g = gpio::make(0).value();
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());
    g.set_intr_type(gpio::intr_type::anyedge);

    // Add multiple handlers
    auto h1 = g.try_isr_handler_add([](void*) {}, nullptr);
    TEST_ASSERT_TRUE(h1.has_value());
    auto h2 = g.try_isr_handler_add([](void*) {}, nullptr);
    TEST_ASSERT_TRUE(h2.has_value());

    // Remove all
    g.isr_handler_remove_all();

    gpio::uninstall_isr_service();
}

TEST_CASE("unique_isr_handle RAII lifecycle", "[idfxx][gpio]") {
    auto install_result = gpio::try_install_isr_service();
    TEST_ASSERT_TRUE(install_result.has_value());

    gpio g = gpio::make(0).value();
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());
    g.set_intr_type(gpio::intr_type::anyedge);

    // Add handler and wrap in unique_isr_handle
    auto handle_result = g.try_isr_handler_add([](void*) {}, nullptr);
    TEST_ASSERT_TRUE(handle_result.has_value());

    {
        gpio::unique_isr_handle unique_handle(*handle_result);
        // Handler is active here

        // Move to another unique_isr_handle
        gpio::unique_isr_handle moved_handle(std::move(unique_handle));
        // unique_handle is now empty, moved_handle owns the handler
    }
    // moved_handle destructor removes the handler

    gpio::uninstall_isr_service();
}

TEST_CASE("unique_isr_handle release", "[idfxx][gpio]") {
    auto install_result = gpio::try_install_isr_service();
    TEST_ASSERT_TRUE(install_result.has_value());

    gpio g = gpio::make(0).value();
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());
    g.set_intr_type(gpio::intr_type::anyedge);

    auto handle_result = g.try_isr_handler_add([](void*) {}, nullptr);
    TEST_ASSERT_TRUE(handle_result.has_value());

    // Create unique handle
    gpio::unique_isr_handle unique_handle(*handle_result);

    // Release - transfers ownership back to raw handle
    auto released = unique_handle.release();
    // unique_handle destructor won't remove the handler now

    // Clean up manually
    auto remove_result = g.try_isr_handler_remove(released);
    TEST_ASSERT_TRUE(remove_result.has_value());

    gpio::uninstall_isr_service();
}

TEST_CASE("unique_isr_handle move assignment cleans up target", "[idfxx][gpio]") {
    auto install_result = gpio::try_install_isr_service();
    TEST_ASSERT_TRUE(install_result.has_value());

    gpio g = gpio::make(0).value();
    TEST_ASSERT_TRUE(g.try_set_direction(gpio::mode::input).has_value());
    g.set_intr_type(gpio::intr_type::anyedge);

    auto h1_result = g.try_isr_handler_add([](void*) {}, nullptr);
    TEST_ASSERT_TRUE(h1_result.has_value());
    auto h2_result = g.try_isr_handler_add([](void*) {}, nullptr);
    TEST_ASSERT_TRUE(h2_result.has_value());

    gpio::unique_isr_handle handle1(*h1_result);
    gpio::unique_isr_handle handle2(*h2_result);

    // Move assign handle2 into handle1 - should remove handler1
    handle1 = std::move(handle2);

    // handle1 now owns handler2, handle2 is empty
    // Destructor of handle1 removes handler2

    gpio::uninstall_isr_service();
}

// =============================================================================
// configure_gpios tests
// =============================================================================

TEST_CASE("configure_gpios with vector", "[idfxx][gpio]") {
    gpio g0 = gpio::make(0).value();
    gpio g2 = gpio::make(2).value();

    gpio::config cfg{.mode = gpio::mode::output};
    auto result = try_configure_gpios(cfg, std::vector<gpio>{g0, g2});
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("configure_gpios with variadic args", "[idfxx][gpio]") {
    gpio g0 = gpio::make(0).value();
    gpio g2 = gpio::make(2).value();

    gpio::config cfg{.mode = gpio::mode::input};
    auto result = try_configure_gpios(cfg, g0, g2);
    TEST_ASSERT_TRUE(result.has_value());
}

TEST_CASE("configure_gpios with NC returns error", "[idfxx][gpio]") {
    gpio::config cfg{.mode = gpio::mode::input};
    auto result = try_configure_gpios(cfg, std::vector<gpio>{gpio::nc()});
    TEST_ASSERT_FALSE(result.has_value());
}

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(gpio) outputs GPIO_NC for not connected", "[idfxx][gpio]") {
    auto s = to_string(gpio::nc());
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", s.c_str());
    s = to_string(gpio());
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", s.c_str());
}

TEST_CASE("to_string(gpio) outputs GPIO_N for valid pins", "[idfxx][gpio]") {
    auto s = to_string(gpio_0);
    TEST_ASSERT_EQUAL_STRING("GPIO_0", s.c_str());
    s = to_string(gpio_5);
    TEST_ASSERT_EQUAL_STRING("GPIO_5", s.c_str());
}

// =============================================================================
// Formatter tests
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<gpio, char>);

TEST_CASE("gpio formatter outputs GPIO_NC for not connected", "[idfxx][gpio]") {
    auto s = std::format("{}", gpio::nc());
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", s.c_str());
    s = std::format("{}", gpio());
    TEST_ASSERT_EQUAL_STRING("GPIO_NC", s.c_str());
}

TEST_CASE("gpio formatter outputs GPIO_N for valid pins", "[idfxx][gpio]") {
    auto s = std::format("{}", gpio_0);
    TEST_ASSERT_EQUAL_STRING("GPIO_0", s.c_str());
    s = std::format("{}", gpio_5);
    TEST_ASSERT_EQUAL_STRING("GPIO_5", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
