// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/endpoint>

#include <type_traits>
#include <unity.h>

static_assert(std::is_default_constructible_v<idfxx::net::endpoint>);
static_assert(std::is_copy_constructible_v<idfxx::net::endpoint>);
static_assert(std::is_move_constructible_v<idfxx::net::endpoint>);

TEST_CASE("endpoint default is empty", "[net]") {
    idfxx::net::endpoint ep;
    TEST_ASSERT_FALSE(ep.family().has_value());
    TEST_ASSERT_EQUAL_UINT16(0, ep.port());
    TEST_ASSERT_FALSE(ep.address<idfxx::net::ipv4_addr>().has_value());
    TEST_ASSERT_FALSE(ep.address<idfxx::net::ipv6_addr>().has_value());
}

TEST_CASE("endpoint v4 round-trip", "[net]") {
    idfxx::net::endpoint ep(idfxx::net::ipv4_addr(192, 168, 1, 1), 8080);
    TEST_ASSERT_TRUE(ep.family() == idfxx::net::family::ipv4);
    TEST_ASSERT_EQUAL_UINT16(8080, ep.port());
    TEST_ASSERT_TRUE(ep.address<idfxx::net::ipv4_addr>().has_value());
    TEST_ASSERT_FALSE(ep.address<idfxx::net::ipv6_addr>().has_value());

    auto s = idfxx::to_string(ep);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1:8080", s.c_str());
}

TEST_CASE("endpoint v6 round-trip", "[net]") {
    idfxx::net::endpoint ep(*idfxx::net::ipv6_addr::parse("fe80::1"), 80);
    TEST_ASSERT_TRUE(ep.family() == idfxx::net::family::ipv6);
    TEST_ASSERT_EQUAL_UINT16(80, ep.port());

    auto s = idfxx::to_string(ep);
    TEST_ASSERT_EQUAL_STRING("[fe80::1]:80", s.c_str());
}

TEST_CASE("endpoint::parse v4", "[net]") {
    auto ep = idfxx::net::endpoint::parse("10.0.0.1:1234");
    TEST_ASSERT_TRUE(ep.has_value());
    TEST_ASSERT_TRUE(ep->family() == idfxx::net::family::ipv4);
    TEST_ASSERT_EQUAL_UINT16(1234, ep->port());
}

TEST_CASE("endpoint::parse v6", "[net]") {
    auto ep = idfxx::net::endpoint::parse("[2001:db8::1]:443");
    TEST_ASSERT_TRUE(ep.has_value());
    TEST_ASSERT_TRUE(ep->family() == idfxx::net::family::ipv6);
    TEST_ASSERT_EQUAL_UINT16(443, ep->port());
}

TEST_CASE("endpoint::parse rejects malformed input", "[net]") {
    TEST_ASSERT_FALSE(idfxx::net::endpoint::parse("").has_value());
    TEST_ASSERT_FALSE(idfxx::net::endpoint::parse("1.2.3.4").has_value());
    TEST_ASSERT_FALSE(idfxx::net::endpoint::parse("1.2.3.4:abc").has_value());
    TEST_ASSERT_FALSE(idfxx::net::endpoint::parse("1.2.3.4:65536").has_value());
    TEST_ASSERT_FALSE(idfxx::net::endpoint::parse("[fe80::1]").has_value());
    TEST_ASSERT_FALSE(idfxx::net::endpoint::parse("[fe80::1]:").has_value());
}

TEST_CASE("endpoint equality", "[net]") {
    idfxx::net::endpoint a(idfxx::net::ipv4_addr(127, 0, 0, 1), 80);
    idfxx::net::endpoint b(idfxx::net::ipv4_addr(127, 0, 0, 1), 80);
    idfxx::net::endpoint c(idfxx::net::ipv4_addr(127, 0, 0, 1), 81);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}
