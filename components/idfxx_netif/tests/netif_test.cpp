// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/netif>

#include <esp_netif.h>
#include <type_traits>
#include <unity.h>

// =============================================================================
// Static assertions — type properties
// =============================================================================

static_assert(std::is_default_constructible_v<idfxx::net::ipv4_addr>);
static_assert(std::is_trivially_copyable_v<idfxx::net::ipv4_addr>);
static_assert(std::is_default_constructible_v<idfxx::net::ipv6_addr>);
static_assert(std::is_aggregate_v<idfxx::net::ipv4_info>);
static_assert(std::is_aggregate_v<idfxx::net::ipv6_info>);
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

TEST_CASE("ipv4_addr default is zero", "[netif]") {
    idfxx::net::ipv4_addr addr;
    TEST_ASSERT_EQUAL_UINT32(0, addr.addr());
    TEST_ASSERT_TRUE(addr.is_any());
}

TEST_CASE("ipv4_addr from octets", "[netif]") {
    idfxx::net::ipv4_addr addr(192, 168, 1, 100);
    TEST_ASSERT_FALSE(addr.is_any());
    auto s = idfxx::to_string(addr);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", s.c_str());
}

TEST_CASE("ipv4_addr equality", "[netif]") {
    idfxx::net::ipv4_addr a(10, 0, 0, 1);
    idfxx::net::ipv4_addr b(10, 0, 0, 1);
    idfxx::net::ipv4_addr c(10, 0, 0, 2);
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
}

TEST_CASE("ipv4_info to_string", "[netif]") {
    idfxx::net::ipv4_info info{
        .ip = idfxx::net::ipv4_addr(192, 168, 1, 100),
        .netmask = idfxx::net::ipv4_addr(255, 255, 255, 0),
        .gateway = idfxx::net::ipv4_addr(192, 168, 1, 1),
    };
    auto s = idfxx::to_string(info);
    TEST_ASSERT_EQUAL_STRING("ip=192.168.1.100, netmask=255.255.255.0, gw=192.168.1.1", s.c_str());
}

TEST_CASE("dns_type to_string", "[netif]") {
    auto s = idfxx::to_string(idfxx::netif::dns_type::main);
    TEST_ASSERT_EQUAL_STRING("main", s.c_str());
    s = idfxx::to_string(idfxx::netif::dns_type::backup);
    TEST_ASSERT_EQUAL_STRING("backup", s.c_str());
    s = idfxx::to_string(idfxx::netif::dns_type::fallback);
    TEST_ASSERT_EQUAL_STRING("fallback", s.c_str());
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
    auto s = ec.message();
    TEST_ASSERT_EQUAL_STRING("Invalid parameters", s.c_str());
}

TEST_CASE("flags bitwise operations", "[netif]") {
    auto f = idfxx::flags(idfxx::netif::flag::dhcp_client) | idfxx::netif::flag::autoup;
    TEST_ASSERT_TRUE(f.contains(idfxx::netif::flag::dhcp_client));
    TEST_ASSERT_TRUE(f.contains(idfxx::netif::flag::autoup));
    TEST_ASSERT_FALSE(f.contains(idfxx::netif::flag::dhcp_server));
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0) && defined(CONFIG_LWIP_ND6_SUPPORT_STATIC_ENTRIES)
TEST_CASE("static neighbor add and remove", "[netif]") {
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    auto netif = idfxx::netif::interface::take(esp_netif_new(&cfg));
    auto addr = *idfxx::net::ipv6_addr::parse("fe80::1");
    auto mac = idfxx::mac_address(0x02, 0x00, 0x00, 0x00, 0x00, 0x01);

    auto r = netif.try_remove_static_neighbor(addr);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::not_found);

    TEST_ASSERT_TRUE(netif.try_add_static_neighbor(addr, mac).has_value());
    // Re-adding the same address updates the entry
    TEST_ASSERT_TRUE(netif.try_add_static_neighbor(addr, idfxx::mac_address(0x02, 0, 0, 0, 0, 0x02)).has_value());
    TEST_ASSERT_TRUE(netif.try_remove_static_neighbor(addr).has_value());

    r = netif.try_remove_static_neighbor(addr);
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::not_found);
}
#endif
