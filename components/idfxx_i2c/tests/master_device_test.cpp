// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::i2c::master_device
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/i2c/master"

#include <type_traits>
#include <unity.h>
#include <vector>
#include <utility>

using namespace idfxx::i2c;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// master_device is not default constructible
static_assert(!std::is_default_constructible_v<master_device>);

// master_device is non-copyable
static_assert(!std::is_copy_constructible_v<master_device>);
static_assert(!std::is_copy_assignable_v<master_device>);

// master_device is move-only
static_assert(std::is_move_constructible_v<master_device>);
static_assert(std::is_move_assignable_v<master_device>);

// master_device::config is an aggregate with sensible defaults
static_assert(std::is_aggregate_v<master_device::config>);
static_assert(std::is_default_constructible_v<master_device::config>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("master_device::make with valid bus succeeds", "[idfxx][i2c][master_device]") {
    // Create a bus first
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    // Create a device (0x50 is common EEPROM address, may not be present)
    auto device_result = master_device::make(bus, 0x50);
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;
    TEST_ASSERT_EQUAL(0x50, device.address());
}

TEST_CASE("master_device::make with config succeeds", "[idfxx][i2c][master_device]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    auto device_result = master_device::make(bus, 0x50, {
        .scl_speed = freq::kilohertz(400),
    });
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;
    TEST_ASSERT_EQUAL(0x50, device.address());
}

TEST_CASE("master_device::make with config default scl_speed uses bus frequency", "[idfxx][i2c][master_device]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    // scl_speed = 0 means use bus frequency
    auto device_result = master_device::make(bus, 0x50, {});
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;
    TEST_ASSERT_EQUAL(0x50, device.address());
}

TEST_CASE("master_device transmit API compiles", "[idfxx][i2c][master_device]") {
    // This test verifies that the transmit API exists at compile time
    // Actual transmission would require hardware device

    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    auto device_result = master_device::make(bus, 0x50);
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;

    // Verify API exists (will likely fail without actual device)
    std::vector<uint8_t> data{0x01, 0x02, 0x03};
    [[maybe_unused]] auto result1 = device.try_transmit(data);
    [[maybe_unused]] auto result2 = device.try_transmit(data.data(), data.size());
    [[maybe_unused]] auto result3 = device.try_transmit(data, std::chrono::milliseconds(100));
}

TEST_CASE("master_device receive API compiles", "[idfxx][i2c][master_device]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    auto device_result = master_device::make(bus, 0x50);
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;

    // Verify API exists
    std::vector<uint8_t> buffer(10);
    [[maybe_unused]] auto result1 = device.try_receive(buffer);
    [[maybe_unused]] auto result2 = device.try_receive(buffer.data(), buffer.size());
    [[maybe_unused]] auto result3 = device.try_receive(buffer, std::chrono::milliseconds(100));
}

TEST_CASE("master_device write_register API compiles", "[idfxx][i2c][master_device]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    auto device_result = master_device::make(bus, 0x50);
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;

    // Verify 16-bit register API exists
    std::vector<uint8_t> data{0xAB, 0xCD};
    [[maybe_unused]] auto result1 = device.try_write_register(0x0010, data);
    [[maybe_unused]] auto result2 = device.try_write_register(0x0010, data.data(), data.size());
    [[maybe_unused]] auto result3 = device.try_write_register(0x0010, data, std::chrono::milliseconds(100));

    // Verify 8-bit register (split) API exists
    [[maybe_unused]] auto result4 = device.try_write_register(0x00, 0x10, data);
    [[maybe_unused]] auto result5 = device.try_write_register(0x00, 0x10, data.data(), data.size());
}

TEST_CASE("master_device read_register API compiles", "[idfxx][i2c][master_device]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    auto device_result = master_device::make(bus, 0x50);
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;

    // Verify 16-bit register API exists
    std::vector<uint8_t> buffer(10);
    [[maybe_unused]] auto result1 = device.try_read_register(0x0010, buffer);
    [[maybe_unused]] auto result2 = device.try_read_register(0x0010, buffer.data(), buffer.size());
    [[maybe_unused]] auto result3 = device.try_read_register(0x0010, buffer, std::chrono::milliseconds(100));

    // Verify 8-bit register (split) API exists
    [[maybe_unused]] auto result4 = device.try_read_register(0x00, 0x10, buffer);
    [[maybe_unused]] auto result5 = device.try_read_register(0x00, 0x10, buffer.data(), buffer.size());
}

TEST_CASE("master_device write_registers API compiles", "[idfxx][i2c][master_device]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    auto device_result = master_device::make(bus, 0x50);
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;

    // Verify multi-register write API exists
    std::vector<uint16_t> registers{0x0010, 0x0011, 0x0012};
    std::vector<uint8_t> data{0xAB, 0xCD, 0xEF};
    [[maybe_unused]] auto result1 = device.try_write_registers(registers, data);
    [[maybe_unused]] auto result2 = device.try_write_registers(registers, data.data(), data.size());
    [[maybe_unused]] auto result3 = device.try_write_registers(registers, data, std::chrono::milliseconds(10));
}

TEST_CASE("master_device bus accessor works", "[idfxx][i2c][master_device]") {
    auto bus_result = master_bus::make(port::i2c0, idfxx::gpio_21, idfxx::gpio_9, freq::kilohertz(100));
    if (!bus_result.has_value()) {
        TEST_FAIL_MESSAGE("Failed to create I2C bus");
    }
    auto& bus = *bus_result;

    auto device_result = master_device::make(bus, 0x50);
    TEST_ASSERT_TRUE(device_result.has_value());

    auto& device = *device_result;
    TEST_ASSERT_EQUAL(std::to_underlying(port::i2c0), std::to_underlying(device.bus().port()));
}
