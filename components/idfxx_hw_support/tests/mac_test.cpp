// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx MAC address types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/mac.hpp"
#include "unity.h"

#include <esp_mac.h>
#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::is_enum_v<mac_type>);
static_assert(std::is_same_v<std::underlying_type_t<mac_type>, int>);

// mac_type values match ESP-IDF constants
static_assert(static_cast<int>(mac_type::wifi_sta) == ESP_MAC_WIFI_STA);
static_assert(static_cast<int>(mac_type::wifi_softap) == ESP_MAC_WIFI_SOFTAP);
static_assert(static_cast<int>(mac_type::bt) == ESP_MAC_BT);
static_assert(static_cast<int>(mac_type::ethernet) == ESP_MAC_ETH);
#if SOC_IEEE802154_SUPPORTED
static_assert(static_cast<int>(mac_type::ieee802154) == ESP_MAC_IEEE802154);
#endif
static_assert(static_cast<int>(mac_type::base) == ESP_MAC_BASE);
static_assert(static_cast<int>(mac_type::efuse_factory) == ESP_MAC_EFUSE_FACTORY);
static_assert(static_cast<int>(mac_type::efuse_custom) == ESP_MAC_EFUSE_CUSTOM);
#if SOC_IEEE802154_SUPPORTED
static_assert(static_cast<int>(mac_type::efuse_ext) == ESP_MAC_EFUSE_EXT);
#endif

// mac_address properties
static_assert(std::is_default_constructible_v<mac_address>);
static_assert(std::is_trivially_copyable_v<mac_address>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// mac_address construction

TEST_CASE("mac_address default is all zeros", "[idfxx][hw_support][mac]") {
    mac_address addr;
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, addr[i]);
    }
}

TEST_CASE("mac_address from array", "[idfxx][hw_support][mac]") {
    std::array<uint8_t, 6> bytes = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    mac_address addr(bytes);
    TEST_ASSERT_EQUAL_UINT8(0xAA, addr[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, addr[5]);
}

TEST_CASE("mac_address from individual bytes", "[idfxx][hw_support][mac]") {
    mac_address addr(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
    TEST_ASSERT_EQUAL_UINT8(0x11, addr[0]);
    TEST_ASSERT_EQUAL_UINT8(0x66, addr[5]);
}

TEST_CASE("mac_address equality", "[idfxx][hw_support][mac]") {
    mac_address a(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
    mac_address b(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
    mac_address c(0x11, 0x22, 0x33, 0x44, 0x55, 0x67);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

// to_string

TEST_CASE("to_string(mac_address) formats correctly", "[idfxx][hw_support][mac]") {
    mac_address addr(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
    auto s = to_string(addr);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", s.c_str());
}

TEST_CASE("to_string(mac_address) formats zeros", "[idfxx][hw_support][mac]") {
    mac_address addr;
    auto s = to_string(addr);
    TEST_ASSERT_EQUAL_STRING("00:00:00:00:00:00", s.c_str());
}

// MAC reading

TEST_CASE("try_base_mac_address returns valid MAC", "[idfxx][hw_support][mac]") {
    auto r = try_base_mac_address();
    TEST_ASSERT_TRUE(r.has_value());
}

TEST_CASE("try_default_mac returns valid MAC", "[idfxx][hw_support][mac]") {
    auto r = try_default_mac();
    TEST_ASSERT_TRUE(r.has_value());
}

TEST_CASE("try_read_mac with base type succeeds", "[idfxx][hw_support][mac]") {
    auto r = try_read_mac(mac_type::base);
    TEST_ASSERT_TRUE(r.has_value());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<mac_address, char>);

TEST_CASE("mac_address formatter", "[idfxx][hw_support][mac]") {
    mac_address addr(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
    auto s = std::format("{}", addr);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
