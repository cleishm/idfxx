// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/netif>

#include <esp_netif.h>
#include <limits>
#include <string>
#include <type_traits>
#include <unity.h>

#ifdef CONFIG_LWIP_DHCPS
#include <dhcpserver/dhcpserver.h>
#endif

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

namespace {
int custom_destroy_count = 0;

void custom_destroy(esp_netif_t* handle) {
    ++custom_destroy_count;
    esp_netif_destroy(handle);
}
} // namespace

TEST_CASE("take with a custom destroy calls it exactly once", "[netif]") {
    custom_destroy_count = 0;
    // Two netifs are alive at once, so each needs its own key
    esp_netif_inherent_config_t base_a = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_a.if_key = "TAKE_A";
    esp_netif_inherent_config_t base_b = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_b.if_key = "TAKE_B";
    esp_netif_config_t cfg_a = ESP_NETIF_DEFAULT_ETH();
    cfg_a.base = &base_a;
    esp_netif_config_t cfg_b = ESP_NETIF_DEFAULT_ETH();
    cfg_b.base = &base_b;
    {
        auto netif = idfxx::netif::interface::take(esp_netif_new(&cfg_a), custom_destroy);
        auto moved = std::move(netif);
        TEST_ASSERT_EQUAL(0, custom_destroy_count);

        // Move-assigning over an owning interface destroys its previous handle
        moved = idfxx::netif::interface::take(esp_netif_new(&cfg_b), custom_destroy);
        TEST_ASSERT_EQUAL(1, custom_destroy_count);
    }
    TEST_ASSERT_EQUAL(2, custom_destroy_count);
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

#ifdef CONFIG_LWIP_DHCPS
namespace {

constexpr uint32_t lease_unit = CONFIG_LWIP_DHCPS_LEASE_UNIT;
constexpr uint32_t max_lease_units = std::numeric_limits<uint32_t>::max() / lease_unit;

const esp_netif_ip_info_t dhcps_ip_info = {
    .ip = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)},
    .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
    .gw = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)},
};

// An Ethernet-stack interface running a DHCP server on 192.168.4.1/24. Without the autoup flag the
// interface never comes up, so its server stays in the initial state, where options can be both
// set and read back; with it, starting the interface starts the server.
idfxx::netif::interface make_dhcps_netif(const char* key, bool autoup = false) {
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.flags = static_cast<esp_netif_flags_t>(ESP_NETIF_DHCP_SERVER | (autoup ? ESP_NETIF_FLAG_AUTOUP : 0));
    base.ip_info = &dhcps_ip_info;
    base.if_key = key;
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    cfg.base = &base;
    esp_netif_t* handle = esp_netif_new(&cfg);
    TEST_ASSERT_NOT_NULL(handle);
    return idfxx::netif::interface::take(handle);
}

uint32_t get_lease_units(const idfxx::netif::interface& netif) {
    dhcps_time_t units = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_netif_dhcps_option(netif.idf_handle(), ESP_NETIF_OP_GET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &units, sizeof(units)));
    return units;
}

dhcps_lease_t get_lease_range(const idfxx::netif::interface& netif) {
    dhcps_lease_t range{};
    TEST_ASSERT_EQUAL(ESP_OK, esp_netif_dhcps_option(netif.idf_handle(), ESP_NETIF_OP_GET, ESP_NETIF_REQUESTED_IP_ADDRESS, &range, sizeof(range)));
    return range;
}

bool get_offer(const idfxx::netif::interface& netif, esp_netif_dhcp_option_id_t id) {
    dhcps_offer_t offer = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_netif_dhcps_option(netif.idf_handle(), ESP_NETIF_OP_GET, id, &offer, sizeof(offer)));
    return offer != 0;
}

uint32_t expected_units(uint32_t seconds) {
    return (seconds + lease_unit - 1) / lease_unit;
}

} // namespace

TEST_CASE("dhcp server config applies options before starting", "[netif]") {
    auto netif = make_dhcps_netif("DHCPS_CFG");

    // Driver defaults, which the config below changes
    auto default_units = get_lease_units(netif);
    TEST_ASSERT_NOT_EQUAL(expected_units(61), default_units);
    TEST_ASSERT_FALSE(get_lease_range(netif).enable);
    TEST_ASSERT_TRUE(get_offer(netif, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS));
    TEST_ASSERT_FALSE(get_offer(netif, ESP_NETIF_DOMAIN_NAME_SERVER));

    auto r = netif.try_dhcp_server_start({
        .lease_time = std::chrono::seconds(61),
        .leases = {{
            .first = idfxx::net::ipv4_addr(192, 168, 4, 10),
            .last = idfxx::net::ipv4_addr(192, 168, 4, 20),
        }},
        .offer_router = false,
        .offer_dns = true,
        .captive_portal_uri = "http://192.168.4.1/portal",
    });
    TEST_ASSERT_TRUE(r.has_value());
    // The interface is down, so the server is armed but not running
    TEST_ASSERT_FALSE(netif.is_dhcp_server_running());

    // Rounded up to whole lease units (2 at the default unit of 60 s)
    TEST_ASSERT_EQUAL_UINT32(expected_units(61), get_lease_units(netif));
    auto range = get_lease_range(netif);
    TEST_ASSERT_TRUE(range.enable);
    TEST_ASSERT_EQUAL_UINT32(idfxx::net::ipv4_addr(192, 168, 4, 10).addr(), range.start_ip.addr);
    TEST_ASSERT_EQUAL_UINT32(idfxx::net::ipv4_addr(192, 168, 4, 20).addr(), range.end_ip.addr);
    TEST_ASSERT_FALSE(get_offer(netif, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS));
    TEST_ASSERT_TRUE(get_offer(netif, ESP_NETIF_DOMAIN_NAME_SERVER));

    // Unset fields keep their settings
    r = netif.try_dhcp_server_start({.offer_router = true});
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_EQUAL_UINT32(expected_units(61), get_lease_units(netif));
    TEST_ASSERT_TRUE(get_lease_range(netif).enable);
    TEST_ASSERT_TRUE(get_offer(netif, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS));
    TEST_ASSERT_TRUE(get_offer(netif, ESP_NETIF_DOMAIN_NAME_SERVER));
}

TEST_CASE("dhcp server config applies options after a stop", "[netif]") {
    // As on a WiFi AP interface before WiFi starts: stopping the armed server, then starting it
    // with a configuration
    auto netif = make_dhcps_netif("DHCPS_STOPPED");
    TEST_ASSERT_TRUE(netif.try_dhcp_server_stop().has_value());

    auto r = netif.try_dhcp_server_start({
        .lease_time = std::chrono::seconds(61),
        .offer_dns = true,
        .captive_portal_uri = "http://192.168.4.1/",
    });
    TEST_ASSERT_TRUE(r.has_value());
    TEST_ASSERT_FALSE(netif.is_dhcp_server_running());
    TEST_ASSERT_EQUAL_UINT32(expected_units(61), get_lease_units(netif));
    TEST_ASSERT_TRUE(get_offer(netif, ESP_NETIF_DOMAIN_NAME_SERVER));
}

TEST_CASE("dhcp server config rounds the lease time up to whole units", "[netif]") {
    auto netif = make_dhcps_netif("DHCPS_LEASE");

    TEST_ASSERT_TRUE(netif.try_dhcp_server_start({.lease_time = std::chrono::seconds(1)}).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, get_lease_units(netif));

    TEST_ASSERT_TRUE(netif.try_dhcp_server_start({.lease_time = std::chrono::seconds(3 * lease_unit)}).has_value());
    TEST_ASSERT_EQUAL_UINT32(3, get_lease_units(netif));

    TEST_ASSERT_TRUE(netif.try_dhcp_server_start({.lease_time = std::chrono::seconds(3 * lease_unit + 1)}).has_value());
    TEST_ASSERT_EQUAL_UINT32(4, get_lease_units(netif));

    // The longest lease whose time in seconds fits in 32 bits
    TEST_ASSERT_TRUE(netif.try_dhcp_server_start({.lease_time = std::chrono::seconds(int64_t{max_lease_units} * lease_unit)}).has_value());
    TEST_ASSERT_EQUAL_UINT32(max_lease_units, get_lease_units(netif));
}

TEST_CASE("dhcp server config rejects invalid lease times", "[netif]") {
    auto netif = make_dhcps_netif("DHCPS_BADLEASE");
    auto default_units = get_lease_units(netif);

    for (auto lease : {std::chrono::seconds(0), std::chrono::seconds(-1),
                       std::chrono::seconds(int64_t{max_lease_units} * lease_unit + 1),
                       std::chrono::seconds(std::numeric_limits<int64_t>::max())}) {
        auto r = netif.try_dhcp_server_start({
            .lease_time = lease,
            .offer_dns = true,
        });
        TEST_ASSERT_FALSE(r.has_value());
        TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    }

    // Rejected before any option was applied
    TEST_ASSERT_EQUAL_UINT32(default_units, get_lease_units(netif));
    TEST_ASSERT_FALSE(get_offer(netif, ESP_NETIF_DOMAIN_NAME_SERVER));
    TEST_ASSERT_FALSE(netif.is_dhcp_server_running());
}

TEST_CASE("dhcp server config rejects an invalid lease range", "[netif]") {
    auto netif = make_dhcps_netif("DHCPS_BADRANGE");
    auto default_units = get_lease_units(netif);

    // The range includes the interface's own address
    auto r = netif.try_dhcp_server_start({
        .lease_time = std::chrono::seconds(61),
        .leases = {{
            .first = idfxx::net::ipv4_addr(192, 168, 4, 1),
            .last = idfxx::net::ipv4_addr(192, 168, 4, 20),
        }},
    });
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::netif::errc::invalid_params);
    TEST_ASSERT_FALSE(netif.is_dhcp_server_running());

    // The range is applied first, so nothing else was
    TEST_ASSERT_FALSE(get_lease_range(netif).enable);
    TEST_ASSERT_EQUAL_UINT32(default_units, get_lease_units(netif));

    // Outside the interface's subnet
    r = netif.try_dhcp_server_start({
        .leases = {{
            .first = idfxx::net::ipv4_addr(192, 168, 5, 10),
            .last = idfxx::net::ipv4_addr(192, 168, 5, 20),
        }},
    });
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::netif::errc::invalid_params);
}

TEST_CASE("dhcp server config rejects invalid captive-portal URIs", "[netif]") {
    auto netif = make_dhcps_netif("DHCPS_BADURI");
    auto default_units = get_lease_units(netif);

    for (const auto& uri : {std::string(), std::string("http://a\0b", 10), std::string(248, 'u')}) {
        auto r = netif.try_dhcp_server_start({
            .lease_time = std::chrono::seconds(61),
            .captive_portal_uri = uri,
        });
        TEST_ASSERT_FALSE(r.has_value());
        TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_arg);
    }
    TEST_ASSERT_EQUAL_UINT32(default_units, get_lease_units(netif));

    // The longest URI the server has room for
    TEST_ASSERT_TRUE(netif.try_dhcp_server_start({.captive_portal_uri = std::string(247, 'u')}).has_value());
}

TEST_CASE("dhcp server config URI requires an owning interface", "[netif]") {
    auto owner = make_dhcps_netif("DHCPS_WRAP");
    auto default_units = get_lease_units(owner);
    auto wrapped = idfxx::netif::interface::wrap(owner.idf_handle());

    auto r = wrapped.try_dhcp_server_start({
        .lease_time = std::chrono::seconds(61),
        .captive_portal_uri = "http://192.168.4.1/",
    });
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_state);
    TEST_ASSERT_EQUAL_UINT32(default_units, get_lease_units(owner));

    // Other options can be set through a non-owning interface
    TEST_ASSERT_TRUE(wrapped.try_dhcp_server_start({.lease_time = std::chrono::seconds(61)}).has_value());
    TEST_ASSERT_EQUAL_UINT32(expected_units(61), get_lease_units(owner));
}

TEST_CASE("dhcp server config URI is handed over on move", "[netif]") {
    // The driver can't report the URI back, so this only checks that moving and replacing a URI
    // leaves the interfaces usable
    auto netif = make_dhcps_netif("DHCPS_MOVE_A");
    TEST_ASSERT_TRUE(netif.try_dhcp_server_start({.captive_portal_uri = "http://192.168.4.1/a"}).has_value());

    auto moved = std::move(netif);
    TEST_ASSERT_TRUE(moved.try_dhcp_server_start({.captive_portal_uri = "http://192.168.4.1/b"}).has_value());

    auto other = make_dhcps_netif("DHCPS_MOVE_B");
    TEST_ASSERT_TRUE(other.try_dhcp_server_start({.captive_portal_uri = "http://192.168.4.1/c"}).has_value());
    // Destroys the first interface, then takes over the second along with its URI
    moved = std::move(other);
    TEST_ASSERT_TRUE(moved.try_dhcp_server_start({.lease_time = std::chrono::seconds(61)}).has_value());
    TEST_ASSERT_EQUAL_UINT32(expected_units(61), get_lease_units(moved));
}

TEST_CASE("dhcp server config is not applied while the server runs", "[netif]") {
    // A driver that discards every frame, so the interface can be brought up
    static esp_netif_driver_base_t driver = {
        .post_attach = [](esp_netif_t* handle, esp_netif_iodriver_handle h) {
            esp_netif_driver_ifconfig_t ifconfig = {};
            ifconfig.handle = h;
            ifconfig.transmit = [](void*, void*, size_t) -> esp_err_t { return ESP_OK; };
            return esp_netif_set_driver_config(handle, &ifconfig);
        },
        .netif = nullptr,
    };
    auto netif = make_dhcps_netif("DHCPS_RUNNING", true);
    TEST_ASSERT_EQUAL(ESP_OK, esp_netif_attach(netif.idf_handle(), &driver));
    esp_netif_action_start(netif.idf_handle(), nullptr, 0, nullptr);
    TEST_ASSERT_TRUE(netif.is_dhcp_server_running());
    auto default_units = get_lease_units(netif);
    TEST_ASSERT_NOT_EQUAL(expected_units(61), default_units);

    auto r = netif.try_dhcp_server_start({.lease_time = std::chrono::seconds(61)});
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::netif::errc::dhcp_already_started);
    TEST_ASSERT_EQUAL_UINT32(default_units, get_lease_units(netif));
    TEST_ASSERT_TRUE(netif.is_dhcp_server_running());

    // Once stopped, the options are applied and the server starts again
    TEST_ASSERT_TRUE(netif.try_dhcp_server_stop().has_value());
    TEST_ASSERT_TRUE(netif.try_dhcp_server_start({.lease_time = std::chrono::seconds(61)}).has_value());
    TEST_ASSERT_TRUE(netif.is_dhcp_server_running());
    TEST_ASSERT_EQUAL_UINT32(expected_units(61), get_lease_units(netif));

    esp_netif_action_stop(netif.idf_handle(), nullptr, 0, nullptr);
}
#endif
