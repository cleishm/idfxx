// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::i2c::master_bus
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/i2c/master"

#include <driver/i2c_master.h>
#include <mutex>
#include <type_traits>
#include <unity.h>
#include <utility>

using namespace idfxx::i2c;
using namespace frequency_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// master_bus is not default constructible
static_assert(!std::is_default_constructible_v<master_bus>);

// master_bus is non-copyable
static_assert(!std::is_copy_constructible_v<master_bus>);
static_assert(!std::is_copy_assignable_v<master_bus>);

// master_bus is non-movable
static_assert(!std::is_move_constructible_v<master_bus>);
static_assert(!std::is_move_assignable_v<master_bus>);

// Verify port enum values
static_assert(static_cast<int>(port::i2c0) == 0);
static_assert(std::to_underlying(port::i2c0) == 0);
#if SOC_HP_I2C_NUM >= 2
static_assert(static_cast<int>(port::i2c1) == 1);
static_assert(std::to_underlying(port::i2c1) == 1);
#endif
#if SOC_LP_I2C_NUM >= 1
static_assert(static_cast<int>(port::lp_i2c0) == 2);
static_assert(std::to_underlying(port::lp_i2c0) == 2);
#endif

// DEFAULT_TIMEOUT is accessible
static_assert(DEFAULT_TIMEOUT == std::chrono::milliseconds(50));

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("master_bus::make with null GPIO returns error", "[idfxx][i2c][master_bus]") {
    auto result = master_bus::make(port::i2c0, idfxx::gpio::nc(), idfxx::gpio::nc(), 100_kHz);
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_arg), result.error().value());
}

TEST_CASE("master_bus scan_devices returns vector", "[idfxx][i2c][master_bus]") {
    // Create a bus with valid GPIO pins (adjust pins for your hardware)
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_26, 100_kHz);
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus - check GPIO pins are valid for your hardware");
    }

    auto& bus = *bus_result.value();

    // Scan for devices (may return empty if no devices connected)
    auto devices = bus.scan_devices();
    TEST_ASSERT_TRUE(devices.size() <= 120); // Valid I2C addresses: 0x08-0x77
}

TEST_CASE("master_bus try_probe with invalid address returns error", "[idfxx][i2c][master_bus]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_26, 100_kHz);
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }

    auto& bus = *bus_result.value();

    // Try probing address 0x00 (reserved, should fail)
    auto probe_result = bus.try_probe(0x00);
    TEST_ASSERT_FALSE(probe_result.has_value());
}

TEST_CASE("master_bus is lockable", "[idfxx][i2c][master_bus]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_26, 100_kHz);
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }

    auto& bus = *bus_result.value();

    // Verify lock/unlock methods exist and work
    bus.lock();
    bus.unlock();

    // Verify try_lock exists
    bool locked = bus.try_lock();
    if (locked) {
        bus.unlock();
    }

    // Verify works with std::lock_guard
    {
        std::lock_guard<master_bus> lock(bus);
        // Lock is held here
    }
    // Lock is released here

    // Verify works with std::unique_lock
    {
        std::unique_lock<master_bus> lock(bus);
        TEST_ASSERT_TRUE(lock.owns_lock());
    }
}

TEST_CASE("master_bus frequency accessor works", "[idfxx][i2c][master_bus]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_26, 400_kHz);
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }

    auto& bus = *bus_result.value();
    TEST_ASSERT_EQUAL(400000, bus.frequency().count());
}

TEST_CASE("master_bus port accessor works", "[idfxx][i2c][master_bus]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_26, 100_kHz);
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }

    auto& bus = *bus_result.value();
    TEST_ASSERT_EQUAL(port::i2c0, bus.port());
    TEST_ASSERT_EQUAL(std::to_underlying(port::i2c0), std::to_underlying(bus.port()));
}

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(port) outputs correct names", "[idfxx][i2c][master_bus]") {
    TEST_ASSERT_EQUAL_STRING("I2C0", idfxx::to_string(port::i2c0).c_str());
#if SOC_HP_I2C_NUM >= 2
    TEST_ASSERT_EQUAL_STRING("I2C1", idfxx::to_string(port::i2c1).c_str());
#endif
#if SOC_LP_I2C_NUM >= 1
    TEST_ASSERT_EQUAL_STRING("LP_I2C0", idfxx::to_string(port::lp_i2c0).c_str());
#endif
}

TEST_CASE("to_string(port) handles unknown values", "[idfxx][i2c][master_bus]") {
    auto unknown = static_cast<port>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", idfxx::to_string(unknown).c_str());
}

// =============================================================================
// Formatter tests
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<port, char>);

TEST_CASE("i2c::port formatter outputs correct names", "[idfxx][i2c][master_bus]") {
    TEST_ASSERT_EQUAL_STRING("I2C0", std::format("{}", port::i2c0).c_str());
#if SOC_HP_I2C_NUM >= 2
    TEST_ASSERT_EQUAL_STRING("I2C1", std::format("{}", port::i2c1).c_str());
#endif
#if SOC_LP_I2C_NUM >= 1
    TEST_ASSERT_EQUAL_STRING("LP_I2C0", std::format("{}", port::lp_i2c0).c_str());
#endif
}

TEST_CASE("i2c::port formatter handles unknown values", "[idfxx][i2c][master_bus]") {
    auto unknown = static_cast<port>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", std::format("{}", unknown).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
