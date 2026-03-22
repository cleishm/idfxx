// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/netif>

#include <type_traits>
#include <unity.h>

// =============================================================================
// Static assertions — type properties
// =============================================================================

static_assert(std::is_default_constructible_v<idfxx::net::ip4_addr>);
static_assert(std::is_trivially_copyable_v<idfxx::net::ip4_addr>);
static_assert(std::is_default_constructible_v<idfxx::net::ip6_addr>);
static_assert(std::is_aggregate_v<idfxx::net::ip4_info>);
static_assert(std::is_aggregate_v<idfxx::net::ip6_info>);
static_assert(std::is_aggregate_v<idfxx::netif::ip4_event_data>);
static_assert(std::is_aggregate_v<idfxx::netif::ip6_event_data>);
static_assert(std::is_aggregate_v<idfxx::netif::ap_sta_ip4_assigned_event_data>);
static_assert(std::is_aggregate_v<idfxx::netif::dns_info>);

// netif is move-only
static_assert(!std::is_copy_constructible_v<idfxx::netif::interface>);
static_assert(!std::is_copy_assignable_v<idfxx::netif::interface>);
static_assert(std::is_move_constructible_v<idfxx::netif::interface>);
static_assert(std::is_move_assignable_v<idfxx::netif::interface>);

// =============================================================================
// Runtime tests
// =============================================================================

TEST_CASE("ip4_addr default is zero", "[netif]") {
    idfxx::net::ip4_addr addr;
    TEST_ASSERT_EQUAL_UINT32(0, addr.addr());
    TEST_ASSERT_TRUE(addr.is_any());
}

TEST_CASE("ip4_addr from octets", "[netif]") {
    idfxx::net::ip4_addr addr(192, 168, 1, 100);
    TEST_ASSERT_FALSE(addr.is_any());
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", s.c_str());
}

TEST_CASE("ip4_addr equality", "[netif]") {
    idfxx::net::ip4_addr a(10, 0, 0, 1);
    idfxx::net::ip4_addr b(10, 0, 0, 1);
    idfxx::net::ip4_addr c(10, 0, 0, 2);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

TEST_CASE("ip4_info to_string", "[netif]") {
    idfxx::net::ip4_info info{
        .ip = idfxx::net::ip4_addr(192, 168, 1, 100),
        .netmask = idfxx::net::ip4_addr(255, 255, 255, 0),
        .gateway = idfxx::net::ip4_addr(192, 168, 1, 1),
    };
    auto s = idfxx::to_string(info);
    TEST_ASSERT_EQUAL_STRING("ip=192.168.1.100, netmask=255.255.255.0, gw=192.168.1.1", s.c_str());
}

TEST_CASE("dns_type to_string", "[netif]") {
    TEST_ASSERT_EQUAL_STRING("main", idfxx::to_string(idfxx::netif::dns_type::main).c_str());
    TEST_ASSERT_EQUAL_STRING("backup", idfxx::to_string(idfxx::netif::dns_type::backup).c_str());
    TEST_ASSERT_EQUAL_STRING("fallback", idfxx::to_string(idfxx::netif::dns_type::fallback).c_str());
}

TEST_CASE("netif init is idempotent", "[netif]") {
    // netif is already initialized by the test harness (main.cpp),
    // so this verifies that re-initialization succeeds
    auto r = idfxx::netif::try_init();
    TEST_ASSERT_TRUE(r.has_value());
}

TEST_CASE("netif error category name", "[netif]") {
    TEST_ASSERT_EQUAL_STRING("netif::Error", idfxx::netif_category().name());
}

TEST_CASE("netif error category message", "[netif]") {
    auto ec = make_error_code(idfxx::netif::errc::invalid_params);
    TEST_ASSERT_EQUAL_STRING("Invalid parameters", ec.message().c_str());
}

TEST_CASE("flags bitwise operations", "[netif]") {
    auto f = idfxx::flags(idfxx::netif::flag::dhcp_client) | idfxx::netif::flag::autoup;
    TEST_ASSERT_TRUE(f.contains(idfxx::netif::flag::dhcp_client));
    TEST_ASSERT_TRUE(f.contains(idfxx::netif::flag::autoup));
    TEST_ASSERT_FALSE(f.contains(idfxx::netif::flag::dhcp_server));
}
