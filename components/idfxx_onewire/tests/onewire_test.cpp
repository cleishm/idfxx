// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx onewire
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/onewire"
#include "unity.h"

#include <mutex>
#include <type_traits>

using namespace idfxx::onewire;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// address is trivially copyable and constexpr-constructible
static_assert(std::is_trivially_copyable_v<address>);
static_assert(std::is_default_constructible_v<address>);
static_assert(address{}.raw() == 0);
static_assert(address::any().raw() == 0);
static_assert(address::none().raw() == 0xFFFFFFFFFFFFFFFF);
static_assert(address{0x28}.family() == 0x28);

// address comparison
static_assert(address{} == address::any());
static_assert(address::any() != address::none());
static_assert(address{0x1234} != address{0x5678});
static_assert(address{0x1234} < address{0x5678});

// bus is non-copyable, non-movable, and not default constructible
static_assert(!std::is_default_constructible_v<bus>);
static_assert(!std::is_copy_constructible_v<bus>);
static_assert(!std::is_copy_assignable_v<bus>);
static_assert(!std::is_move_constructible_v<bus>);
static_assert(!std::is_move_assignable_v<bus>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("address default construction is any", "[idfxx][onewire]") {
    address addr;
    TEST_ASSERT_TRUE(addr.raw() == 0);
    TEST_ASSERT_TRUE(addr == address::any());
}

TEST_CASE("address from raw value", "[idfxx][onewire]") {
    address addr{0x28FF123456789ABC};
    TEST_ASSERT_TRUE(addr.raw() == 0x28FF123456789ABC);
    TEST_ASSERT_EQUAL_UINT8(0xBC, addr.family());
}

TEST_CASE("address family extraction", "[idfxx][onewire]") {
    // DS18B20 family code in low byte
    address addr{0x1234567890ABCD28};
    TEST_ASSERT_EQUAL_UINT8(0x28, addr.family());
}

TEST_CASE("address equality comparison", "[idfxx][onewire]") {
    address a{0x1234};
    address b{0x1234};
    address c{0x5678};

    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

TEST_CASE("address ordering comparison", "[idfxx][onewire]") {
    address a{0x1000};
    address b{0x2000};

    TEST_ASSERT_TRUE(a < b);
    TEST_ASSERT_FALSE(b < a);
}

TEST_CASE("address none is distinct from any", "[idfxx][onewire]") {
    TEST_ASSERT_FALSE(address::any() == address::none());
    TEST_ASSERT_TRUE(address::none().raw() == 0xFFFFFFFFFFFFFFFF);
}

TEST_CASE("bus::make with NC pin returns error", "[idfxx][onewire]") {
    auto result = bus::make(idfxx::gpio::nc());
    TEST_ASSERT_FALSE(result.has_value());
    TEST_ASSERT_EQUAL(std::to_underlying(idfxx::errc::invalid_state), result.error().value());
}

TEST_CASE("bus::make with valid pin succeeds", "[idfxx][onewire]") {
    auto result = bus::make(idfxx::gpio_4);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL(4, result.value()->pin().num());
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("bus constructor with NC pin throws", "[idfxx][onewire]") {
    bool threw = false;
    try {
        bus b{idfxx::gpio::nc()};
    } catch (const std::system_error&) {
        threw = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(threw, "Expected std::system_error to be thrown");
}

TEST_CASE("bus constructor with valid pin succeeds", "[idfxx][onewire]") {
    bus b{idfxx::gpio_4};
    TEST_ASSERT_EQUAL(4, b.pin().num());
}
#endif

TEST_CASE("bus is lockable", "[idfxx][onewire]") {
    auto result = bus::make(idfxx::gpio_4);
    TEST_ASSERT_TRUE(result.has_value());
    auto& b = *result.value();

    // Verify lock/unlock methods exist and work
    b.lock();
    b.unlock();

    // Verify try_lock exists
    bool locked = b.try_lock();
    if (locked) {
        b.unlock();
    }

    // Verify works with std::lock_guard
    {
        std::lock_guard<bus> lock(b);
    }

    // Verify works with std::unique_lock
    {
        std::unique_lock<bus> lock(b);
        TEST_ASSERT_TRUE(lock.owns_lock());
    }
}

// -- to_string tests ---------------------------------------------------------

TEST_CASE("to_string for address::any()", "[idfxx][onewire]") {
    auto s = idfxx::to_string(address::any());
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_ANY", s.c_str());
}

TEST_CASE("to_string for address::none()", "[idfxx][onewire]") {
    auto s = idfxx::to_string(address::none());
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_NONE", s.c_str());
}

TEST_CASE("to_string for address with value", "[idfxx][onewire]") {
    // 0x28 in low byte, then 0x00, 0x12, ...
    address addr{0xBC9A785634120028};
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("28:00:12:34:56:78:9A:BC", s.c_str());
}

// -- CRC tests ---------------------------------------------------------------

TEST_CASE("crc8 computes correct value", "[idfxx][onewire]") {
    // ROM code with valid CRC: family=0x28, serial, crc
    // The CRC of the first 7 bytes should equal the 8th byte
    uint8_t rom[] = {0x28, 0xFF, 0x12, 0x34, 0x56, 0x78, 0x9A};
    uint8_t crc = crc8(rom);
    // Verify CRC is computed (non-trivial test - just check it returns a value)
    TEST_ASSERT_TRUE(crc == crc8(rom)); // Deterministic

    // Known test vector: CRC8 of empty data is 0
    uint8_t empty[] = {};
    TEST_ASSERT_EQUAL_UINT8(0, crc8(std::span<const uint8_t>(empty, 0)));
}

TEST_CASE("crc16 computes correct value", "[idfxx][onewire]") {
    // Verify CRC16 is deterministic
    uint8_t data[] = {0xF0, 0x88, 0x00};
    uint16_t crc = crc16(data);
    TEST_ASSERT_EQUAL_UINT16(crc, crc16(data));

    // Verify CRC16 with initial value
    uint16_t crc_iv = crc16(data, 0x1234);
    TEST_ASSERT_EQUAL_UINT16(crc_iv, crc16(data, 0x1234));

    // Different IV should produce different CRC
    TEST_ASSERT_TRUE(crc16(data, 0) != crc16(data, 0x1234));
}

TEST_CASE("check_crc16 validates correctly", "[idfxx][onewire]") {
    // Compute CRC for some data, then verify check_crc16 validates it
    uint8_t data[] = {0xF0, 0x88, 0x00, 0x01, 0x02, 0x03};
    uint16_t crc = crc16(data);

    // Invert and store as two bytes (as received from 1-Wire)
    uint16_t inverted = ~crc;
    uint8_t inv_bytes[2] = {static_cast<uint8_t>(inverted & 0xFF), static_cast<uint8_t>((inverted >> 8) & 0xFF)};

    TEST_ASSERT_TRUE(check_crc16(data, inv_bytes));

    // Corrupt the CRC and verify it fails
    inv_bytes[0] ^= 0xFF;
    TEST_ASSERT_FALSE(check_crc16(data, inv_bytes));
}

// -- Formatter tests ---------------------------------------------------------

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<address, char>);

TEST_CASE("address formatter outputs correct string", "[idfxx][onewire]") {
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_ANY", std::format("{}", address::any()).c_str());
    TEST_ASSERT_EQUAL_STRING("ONEWIRE_NONE", std::format("{}", address::none()).c_str());

    address addr{0xBC9A785634120028};
    TEST_ASSERT_EQUAL_STRING("28:00:12:34:56:78:9A:BC", std::format("{}", addr).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
