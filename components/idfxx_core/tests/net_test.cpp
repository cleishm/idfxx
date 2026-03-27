// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::net IP address types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include <idfxx/net>

#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <unity.h>

// =============================================================================
// Compile-time tests (static_assert)
// =============================================================================

// ip4_addr
static_assert(std::is_default_constructible_v<idfxx::net::ip4_addr>);
static_assert(std::is_trivially_copyable_v<idfxx::net::ip4_addr>);
static_assert(sizeof(idfxx::net::ip4_addr) == sizeof(uint32_t));

// ip6_addr
static_assert(std::is_default_constructible_v<idfxx::net::ip6_addr>);

// ip4_info and ip6_info are aggregates
static_assert(std::is_aggregate_v<idfxx::net::ip4_info>);
static_assert(std::is_aggregate_v<idfxx::net::ip6_info>);

// constexpr ip4_addr construction
static_assert(idfxx::net::ip4_addr().is_any());
static_assert(idfxx::net::ip4_addr().addr() == 0);
static_assert(!idfxx::net::ip4_addr(192, 168, 1, 1).is_any());
static_assert(idfxx::net::ip4_addr(192, 168, 1, 1) == idfxx::net::ip4_addr(192, 168, 1, 1));
static_assert(!(idfxx::net::ip4_addr(192, 168, 1, 1) == idfxx::net::ip4_addr(192, 168, 1, 2)));

// constexpr ip6_addr construction
static_assert(idfxx::net::ip6_addr().is_any());
static_assert(!idfxx::net::ip6_addr({1, 0, 0, 0}).is_any());
static_assert(idfxx::net::ip6_addr().zone() == 0);
static_assert(idfxx::net::ip6_addr({0, 0, 0, 0}, 3).zone() == 3);
static_assert(idfxx::net::ip6_addr({1, 2, 3, 4}) == idfxx::net::ip6_addr({1, 2, 3, 4}));
static_assert(!(idfxx::net::ip6_addr({1, 2, 3, 4}) == idfxx::net::ip6_addr({1, 2, 3, 5})));
// Zone affects equality
static_assert(!(idfxx::net::ip6_addr({1, 0, 0, 0}, 1) == idfxx::net::ip6_addr({1, 0, 0, 0}, 2)));

// =============================================================================
// Runtime tests — ip4_addr
// =============================================================================

TEST_CASE("ip4_addr default is zero", "[idfxx][net]") {
    idfxx::net::ip4_addr addr;
    TEST_ASSERT_EQUAL_UINT32(0, addr.addr());
    TEST_ASSERT_TRUE(addr.is_any());
}

TEST_CASE("ip4_addr from octets", "[idfxx][net]") {
    idfxx::net::ip4_addr addr(192, 168, 1, 100);
    TEST_ASSERT_FALSE(addr.is_any());
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", s.c_str());
}

TEST_CASE("ip4_addr from raw uint32", "[idfxx][net]") {
    // 10.0.0.1 in network byte order (little-endian): 0x0100000a
    idfxx::net::ip4_addr addr(0x0100000a);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", s.c_str());
}

TEST_CASE("ip4_addr equality", "[idfxx][net]") {
    idfxx::net::ip4_addr a(10, 0, 0, 1);
    idfxx::net::ip4_addr b(10, 0, 0, 1);
    idfxx::net::ip4_addr c(10, 0, 0, 2);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

TEST_CASE("ip4_addr to_string edge cases", "[idfxx][net]") {
    auto s = idfxx::to_string(idfxx::net::ip4_addr());
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", s.c_str());
    s = idfxx::to_string(idfxx::net::ip4_addr(255, 255, 255, 255));
    TEST_ASSERT_EQUAL_STRING("255.255.255.255", s.c_str());
    s = idfxx::to_string(idfxx::net::ip4_addr(1, 0, 0, 0));
    TEST_ASSERT_EQUAL_STRING("1.0.0.0", s.c_str());
    s = idfxx::to_string(idfxx::net::ip4_addr(0, 0, 0, 1));
    TEST_ASSERT_EQUAL_STRING("0.0.0.1", s.c_str());
}

// =============================================================================
// Runtime tests — ip4_info
// =============================================================================

TEST_CASE("ip4_info to_string", "[idfxx][net]") {
    idfxx::net::ip4_info info{
        .ip = idfxx::net::ip4_addr(192, 168, 1, 100),
        .netmask = idfxx::net::ip4_addr(255, 255, 255, 0),
        .gateway = idfxx::net::ip4_addr(192, 168, 1, 1),
    };
    auto s = idfxx::to_string(info);
    TEST_ASSERT_EQUAL_STRING("ip=192.168.1.100, netmask=255.255.255.0, gw=192.168.1.1", s.c_str());
}

TEST_CASE("ip4_info equality", "[idfxx][net]") {
    idfxx::net::ip4_info a{
        .ip = idfxx::net::ip4_addr(10, 0, 0, 1),
        .netmask = idfxx::net::ip4_addr(255, 0, 0, 0),
        .gateway = idfxx::net::ip4_addr(10, 0, 0, 254),
    };
    idfxx::net::ip4_info b = a;
    TEST_ASSERT_TRUE(a == b);
    b.gateway = idfxx::net::ip4_addr(10, 0, 0, 253);
    TEST_ASSERT_FALSE(a == b);
}

// =============================================================================
// Runtime tests — ip6_addr
// =============================================================================

TEST_CASE("ip6_addr default is zero", "[idfxx][net]") {
    idfxx::net::ip6_addr addr;
    TEST_ASSERT_TRUE(addr.is_any());
    TEST_ASSERT_EQUAL_UINT8(0, addr.zone());
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_UINT32(0, addr.addr()[i]);
    }
}

TEST_CASE("ip6_addr with zone", "[idfxx][net]") {
    idfxx::net::ip6_addr addr({0, 0, 0, 1}, 5);
    TEST_ASSERT_EQUAL_UINT8(5, addr.zone());
    TEST_ASSERT_FALSE(addr.is_any());
}

TEST_CASE("ip6_addr equality includes zone", "[idfxx][net]") {
    idfxx::net::ip6_addr a({1, 2, 3, 4}, 0);
    idfxx::net::ip6_addr b({1, 2, 3, 4}, 0);
    idfxx::net::ip6_addr c({1, 2, 3, 4}, 1);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

// =============================================================================
// Runtime tests — ip6_addr to_string (RFC 5952)
// =============================================================================

// Helper: build an ip6_addr from 8 groups of 16-bit values (in host order).
// The groups are stored into the 4 uint32_t words in network byte order in memory,
// matching lwIP/ESP-IDF convention.
static idfxx::net::ip6_addr ip6_from_groups(
    uint16_t g0, uint16_t g1, uint16_t g2, uint16_t g3,
    uint16_t g4, uint16_t g5, uint16_t g6, uint16_t g7, uint8_t zone = 0
) {
    // Pack groups into bytes in network order, then interpret as uint32_t words
    uint8_t bytes[16];
    auto put = [&](int i, uint16_t g) {
        bytes[i * 2] = uint8_t(g >> 8);
        bytes[i * 2 + 1] = uint8_t(g);
    };
    put(0, g0); put(1, g1); put(2, g2); put(3, g3);
    put(4, g4); put(5, g5); put(6, g6); put(7, g7);

    std::array<uint32_t, 4> words;
    std::memcpy(words.data(), bytes, 16);
    return idfxx::net::ip6_addr(words, zone);
}

TEST_CASE("ip6 to_string all zeros", "[idfxx][net]") {
    auto s = idfxx::to_string(idfxx::net::ip6_addr());
    TEST_ASSERT_EQUAL_STRING("::", s.c_str());
}

TEST_CASE("ip6 to_string loopback", "[idfxx][net]") {
    auto addr = ip6_from_groups(0, 0, 0, 0, 0, 0, 0, 1);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("::1", s.c_str());
}

TEST_CASE("ip6 to_string full address no compression", "[idfxx][net]") {
    auto addr = ip6_from_groups(0x2001, 0x0db8, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("2001:db8:1:2:3:4:5:6", s.c_str());
}

TEST_CASE("ip6 to_string leading zeros compressed", "[idfxx][net]") {
    // ::1:2:3:4:5:6
    auto addr = ip6_from_groups(0, 0, 1, 2, 3, 4, 5, 6);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("::1:2:3:4:5:6", s.c_str());
}

TEST_CASE("ip6 to_string trailing zeros compressed", "[idfxx][net]") {
    // 2001:db8::
    auto addr = ip6_from_groups(0x2001, 0x0db8, 0, 0, 0, 0, 0, 0);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("2001:db8::", s.c_str());
}

TEST_CASE("ip6 to_string middle zeros compressed", "[idfxx][net]") {
    // 2001:db8::1
    auto addr = ip6_from_groups(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", s.c_str());
}

TEST_CASE("ip6 to_string link-local", "[idfxx][net]") {
    // fe80::1
    auto addr = ip6_from_groups(0xfe80, 0, 0, 0, 0, 0, 0, 1);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("fe80::1", s.c_str());
}

TEST_CASE("ip6 to_string single zero group not compressed", "[idfxx][net]") {
    // RFC 5952: a single zero group must NOT be compressed
    // 2001:db8:0:1:2:3:4:5
    auto addr = ip6_from_groups(0x2001, 0x0db8, 0, 1, 2, 3, 4, 5);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("2001:db8:0:1:2:3:4:5", s.c_str());
}

TEST_CASE("ip6 to_string longest run wins", "[idfxx][net]") {
    // 1:0:0:2:0:0:0:3 — longest run is groups 4-6 (length 3), not groups 1-2 (length 2)
    auto addr = ip6_from_groups(1, 0, 0, 2, 0, 0, 0, 3);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("1:0:0:2::3", s.c_str());
}

TEST_CASE("ip6 to_string first longest run wins tie", "[idfxx][net]") {
    // RFC 5952: when two runs are equal length, the first one is compressed
    // 1:0:0:2:3:0:0:4
    auto addr = ip6_from_groups(1, 0, 0, 2, 3, 0, 0, 4);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("1::2:3:0:0:4", s.c_str());
}

TEST_CASE("ip6 to_string multicast", "[idfxx][net]") {
    // ff02::1
    auto addr = ip6_from_groups(0xff02, 0, 0, 0, 0, 0, 0, 1);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("ff02::1", s.c_str());
}

TEST_CASE("ip6 to_string all ones", "[idfxx][net]") {
    auto addr = ip6_from_groups(0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", s.c_str());
}

TEST_CASE("ip6 to_string zone appended", "[idfxx][net]") {
    auto addr = ip6_from_groups(0xfe80, 0, 0, 0, 0, 0, 0, 1, 3);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("fe80::1%3", s.c_str());
}

TEST_CASE("ip6 to_string zeros at both ends prefer first", "[idfxx][net]") {
    // 0:0:1:2:3:4:0:0 — both runs are length 2, first wins
    auto addr = ip6_from_groups(0, 0, 1, 2, 3, 4, 0, 0);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("::1:2:3:4:0:0", s.c_str());
}

TEST_CASE("ip6 to_string groups after compression", "[idfxx][net]") {
    // ff02::1:2
    auto addr = ip6_from_groups(0xff02, 0, 0, 0, 0, 0, 1, 2);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("ff02::1:2", s.c_str());
}

TEST_CASE("ip6 to_string lowercase hex", "[idfxx][net]") {
    auto addr = ip6_from_groups(0xABCD, 0xEF01, 0, 0, 0, 0, 0, 1);
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("abcd:ef01::1", s.c_str());
}

// =============================================================================
// Runtime tests — ip4_addr::parse
// =============================================================================

TEST_CASE("ip4 parse valid addresses", "[idfxx][net]") {
    auto a = idfxx::net::ip4_addr::parse("192.168.1.1");
    TEST_ASSERT_TRUE(a.has_value());
    TEST_ASSERT_TRUE(*a == idfxx::net::ip4_addr(192, 168, 1, 1));

    auto b = idfxx::net::ip4_addr::parse("0.0.0.0");
    TEST_ASSERT_TRUE(b.has_value());
    TEST_ASSERT_TRUE(b->is_any());

    auto c = idfxx::net::ip4_addr::parse("255.255.255.255");
    TEST_ASSERT_TRUE(c.has_value());
    TEST_ASSERT_TRUE(*c == idfxx::net::ip4_addr(255, 255, 255, 255));

    auto d = idfxx::net::ip4_addr::parse("10.0.0.1");
    TEST_ASSERT_TRUE(d.has_value());
    TEST_ASSERT_TRUE(*d == idfxx::net::ip4_addr(10, 0, 0, 1));
}

TEST_CASE("ip4 parse roundtrip", "[idfxx][net]") {
    auto original = idfxx::net::ip4_addr(172, 16, 254, 3);
    auto s = idfxx::to_string(original);
    auto parsed = idfxx::net::ip4_addr::parse(s);
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_TRUE(*parsed == original);
}

TEST_CASE("ip4 parse rejects empty", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("").has_value());
}

TEST_CASE("ip4 parse rejects too few octets", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2.3").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1").has_value());
}

TEST_CASE("ip4 parse rejects too many octets", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2.3.4.5").has_value());
}

TEST_CASE("ip4 parse rejects octet > 255", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("256.0.0.0").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("0.0.0.999").has_value());
}

TEST_CASE("ip4 parse rejects leading zeros", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("01.2.3.4").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.02.3.4").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2.3.04").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("00.0.0.0").has_value());
}

TEST_CASE("ip4 parse rejects trailing chars", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2.3.4 ").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2.3.4x").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2.3.4/24").has_value());
}

TEST_CASE("ip4 parse rejects non-numeric", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("a.b.c.d").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2.3.x").has_value());
}

TEST_CASE("ip4 parse rejects double dot", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1..2.3.4").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip4_addr::parse("1.2..3").has_value());
}

// =============================================================================
// Runtime tests — ip6_addr::parse
// =============================================================================

TEST_CASE("ip6 parse loopback", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("::1");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0, 0, 0, 0, 0, 0, 0, 1);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse all zeros", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("::");
    TEST_ASSERT_TRUE(addr.has_value());
    TEST_ASSERT_TRUE(addr->is_any());
}

TEST_CASE("ip6 parse full address", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("2001:db8:1:2:3:4:5:6");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0x2001, 0x0db8, 1, 2, 3, 4, 5, 6);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse with leading compression", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("::1:2:3:4:5:6");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0, 0, 1, 2, 3, 4, 5, 6);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse with trailing compression", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("2001:db8::");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0x2001, 0x0db8, 0, 0, 0, 0, 0, 0);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse with middle compression", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("2001:db8::1");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse link-local", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("fe80::1");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0xfe80, 0, 0, 0, 0, 0, 0, 1);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse with zone", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("fe80::1%3");
    TEST_ASSERT_TRUE(addr.has_value());
    TEST_ASSERT_EQUAL_UINT8(3, addr->zone());
    auto expected = ip6_from_groups(0xfe80, 0, 0, 0, 0, 0, 0, 1, 3);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse uppercase hex", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("ABCD:EF01::1");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0xABCD, 0xEF01, 0, 0, 0, 0, 0, 1);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse mixed case hex", "[idfxx][net]") {
    auto addr = idfxx::net::ip6_addr::parse("aBcD:eF01::1");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(0xABCD, 0xEF01, 0, 0, 0, 0, 0, 1);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse roundtrip", "[idfxx][net]") {
    auto original = ip6_from_groups(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1);
    auto s = idfxx::to_string(original);
    auto parsed = idfxx::net::ip6_addr::parse(s);
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_TRUE(*parsed == original);
}

TEST_CASE("ip6 parse roundtrip with zone", "[idfxx][net]") {
    auto original = ip6_from_groups(0xfe80, 0, 0, 0, 0, 0, 0, 1, 5);
    auto s = idfxx::to_string(original);
    auto parsed = idfxx::net::ip6_addr::parse(s);
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_TRUE(*parsed == original);
}

TEST_CASE("ip6 parse roundtrip full address", "[idfxx][net]") {
    auto original = ip6_from_groups(0x2001, 0x0db8, 0x85a3, 0x0001, 0x0002, 0x8a2e, 0x0370, 0x7334);
    auto s = idfxx::to_string(original);
    auto parsed = idfxx::net::ip6_addr::parse(s);
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_TRUE(*parsed == original);
}

TEST_CASE("ip6 parse rejects empty", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("").has_value());
}

TEST_CASE("ip6 parse rejects triple colon", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse(":::").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1:::2").has_value());
}

TEST_CASE("ip6 parse rejects double compression", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1::2::3").has_value());
}

TEST_CASE("ip6 parse rejects too many groups", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1:2:3:4:5:6:7:8:9").has_value());
}

TEST_CASE("ip6 parse rejects too few groups without compression", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1:2:3:4:5:6:7").has_value());
}

TEST_CASE("ip6 parse rejects group > 4 hex digits", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("12345::1").has_value());
}

TEST_CASE("ip6 parse rejects non-hex characters", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("gggg::1").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1:2:3:4:5:6:7:xyz").has_value());
}

TEST_CASE("ip6 parse rejects empty group", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1::2:").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse(":1::2").has_value());
}

TEST_CASE("ip6 parse rejects empty zone", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("fe80::1%").has_value());
}

TEST_CASE("ip6 parse rejects non-numeric zone", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("fe80::1%eth0").has_value());
}

TEST_CASE("ip6 parse rejects zone > 255", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("fe80::1%256").has_value());
}

TEST_CASE("ip6 parse rejects trailing chars", "[idfxx][net]") {
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("::1 ").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("::1/64").has_value());
}

TEST_CASE("ip6 parse compression expanding single group", "[idfxx][net]") {
    // 1:2:3:4:5:6:7:: — :: represents exactly one zero group
    auto addr = idfxx::net::ip6_addr::parse("1:2:3:4:5:6:7::");
    TEST_ASSERT_TRUE(addr.has_value());
    auto expected = ip6_from_groups(1, 2, 3, 4, 5, 6, 7, 0);
    TEST_ASSERT_TRUE(*addr == expected);
}

TEST_CASE("ip6 parse too many groups with compression", "[idfxx][net]") {
    // :: can only represent >= 1 group, so 8 explicit + :: is invalid
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1:2:3:4:5:6:7:8::").has_value());
    TEST_ASSERT_FALSE(idfxx::net::ip6_addr::parse("1:2:3:4::5:6:7:8").has_value());
}
