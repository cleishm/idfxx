// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx ds18x20
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/ds18x20"
#include "unity.h"

#include <type_traits>

using namespace idfxx::ds18x20;
using idfxx::onewire::address;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// device is copyable and movable
static_assert(std::is_copy_constructible_v<device>);
static_assert(std::is_copy_assignable_v<device>);
static_assert(std::is_move_constructible_v<device>);
static_assert(std::is_move_assignable_v<device>);

// device is not default constructible
static_assert(!std::is_default_constructible_v<device>);

// family enum values match DS18x20 family codes
static_assert(std::to_underlying(family::ds18s20) == 0x10);
static_assert(std::to_underlying(family::ds1822) == 0x22);
static_assert(std::to_underlying(family::ds18b20) == 0x28);
static_assert(std::to_underlying(family::max31850) == 0x3B);

// resolution enum values match configuration register byte values
static_assert(std::to_underlying(resolution::bits_9) == 0x1F);
static_assert(std::to_underlying(resolution::bits_10) == 0x3F);
static_assert(std::to_underlying(resolution::bits_11) == 0x5F);
static_assert(std::to_underlying(resolution::bits_12) == 0x7F);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("device make with NC pin returns error", "[idfxx][ds18x20]") {
    auto result = device::make(idfxx::gpio::nc());
    TEST_ASSERT_FALSE(result.has_value());
}

TEST_CASE("device make with valid pin succeeds", "[idfxx][ds18x20]") {
    auto result = device::make(idfxx::gpio_4);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(4, result->pin().num());
    TEST_ASSERT_TRUE(result->addr() == address::any());
}

TEST_CASE("device make with address", "[idfxx][ds18x20]") {
    address addr{0x28FF123456789ABC};
    auto result = device::make(idfxx::gpio_4, addr);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_TRUE(result->addr() == addr);
}

TEST_CASE("device equality comparison", "[idfxx][ds18x20]") {
    auto a = *device::make(idfxx::gpio_4, address{0x1234});
    auto b = *device::make(idfxx::gpio_4, address{0x1234});
    auto c = *device::make(idfxx::gpio_4, address{0x5678});

    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

TEST_CASE("device copy semantics", "[idfxx][ds18x20]") {
    auto dev = *device::make(idfxx::gpio_4, address{0x1234});
    auto copy = dev;

    TEST_ASSERT_TRUE(copy == dev);
    TEST_ASSERT_EQUAL(4, copy.pin().num());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("device constructor with NC pin throws", "[idfxx][ds18x20]") {
    bool threw = false;
    try {
        device dev{idfxx::gpio::nc()};
    } catch (const std::system_error&) {
        threw = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}

TEST_CASE("device constructor with valid pin succeeds", "[idfxx][ds18x20]") {
    device dev{idfxx::gpio_4};
    TEST_ASSERT_EQUAL(4, dev.pin().num());
}
#endif

TEST_CASE("try_measure_and_read_multi with empty span returns empty", "[idfxx][ds18x20]") {
    auto result = try_measure_and_read_multi({});
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(0, result->size());
}

TEST_CASE("try_scan_devices with NC pin returns error", "[idfxx][ds18x20]") {
    auto result = try_scan_devices(idfxx::gpio::nc());
    TEST_ASSERT_FALSE(result.has_value());
}

TEST_CASE("to_string for family enum", "[idfxx][ds18x20]") {
    TEST_ASSERT_EQUAL_STRING("DS18S20", idfxx::to_string(family::ds18s20).c_str());
    TEST_ASSERT_EQUAL_STRING("DS1822", idfxx::to_string(family::ds1822).c_str());
    TEST_ASSERT_EQUAL_STRING("DS18B20", idfxx::to_string(family::ds18b20).c_str());
    TEST_ASSERT_EQUAL_STRING("MAX31850", idfxx::to_string(family::max31850).c_str());
}

TEST_CASE("to_string for resolution enum", "[idfxx][ds18x20]") {
    TEST_ASSERT_EQUAL_STRING("9-bit", idfxx::to_string(resolution::bits_9).c_str());
    TEST_ASSERT_EQUAL_STRING("10-bit", idfxx::to_string(resolution::bits_10).c_str());
    TEST_ASSERT_EQUAL_STRING("11-bit", idfxx::to_string(resolution::bits_11).c_str());
    TEST_ASSERT_EQUAL_STRING("12-bit", idfxx::to_string(resolution::bits_12).c_str());
}

TEST_CASE("to_string for unknown family", "[idfxx][ds18x20]") {
    auto s = idfxx::to_string(static_cast<family>(0xFF));
    TEST_ASSERT_EQUAL_STRING("unknown(0xFF)", s.c_str());
}

TEST_CASE("to_string for unknown resolution", "[idfxx][ds18x20]") {
    auto s = idfxx::to_string(static_cast<resolution>(0x00));
    TEST_ASSERT_EQUAL_STRING("unknown(0x00)", s.c_str());
}
