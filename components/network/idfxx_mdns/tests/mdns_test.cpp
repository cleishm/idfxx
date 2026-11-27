// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_mdns
// Uses ESP-IDF Unity test framework with compile-time static_asserts
//
// These run without a network (QEMU has no NIC): they cover the local service
// table, delegated hosts, browsers and query lifecycles. Queries find nothing
// and finish at their timeout. The C library ends a search on its 100 ms timer
// tick, so waits allow a few hundred milliseconds beyond the query timeout.

#include "../src/answer_fill.hpp"
#include "../src/ipv6_zone.hpp"
#include <idfxx/chrono>
#include <idfxx/error>
#include <idfxx/event>
#include <idfxx/mdns>
#include <idfxx/net>
#include <idfxx/netif>
#include <idfxx/sched>
#include <unity.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <limits>
#include <mdns.h>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace idfxx;
using namespace std::chrono_literals;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::is_error_code_enum_v<mdns::errc>);
static_assert(std::to_underlying(mdns::errc::limit_reached) == 1);

static_assert(std::to_underlying(mdns::record_type::a) == 0x01);
static_assert(std::to_underlying(mdns::record_type::ptr) == 0x0C);
static_assert(std::to_underlying(mdns::record_type::txt) == 0x10);
static_assert(std::to_underlying(mdns::record_type::aaaa) == 0x1C);
static_assert(std::to_underlying(mdns::record_type::srv) == 0x21);
static_assert(std::to_underlying(mdns::record_type::any) == 0xFF);

static_assert(std::to_underlying(mdns::transmission::unicast) == 0);
static_assert(std::to_underlying(mdns::transmission::multicast) == 1);

static_assert(std::to_underlying(mdns::netif_action::enable_ipv4) == (1u << 1));
static_assert(std::to_underlying(mdns::netif_action::ipv6_reverse_lookup) == (1u << 8));
static_assert(std::same_as<
              decltype(mdns::netif_action::enable_ipv4 | mdns::netif_action::announce_ipv4),
              flags<mdns::netif_action>>);

static_assert(!std::is_copy_constructible_v<mdns::service>);
static_assert(std::is_nothrow_move_constructible_v<mdns::service>);
static_assert(std::is_nothrow_move_assignable_v<mdns::service>);
static_assert(!std::is_copy_constructible_v<mdns::delegated_host>);
static_assert(std::is_nothrow_move_constructible_v<mdns::delegated_host>);
#ifdef CONFIG_MDNS_ENABLE_BROWSE
static_assert(!std::is_copy_constructible_v<mdns::browser>);
static_assert(std::is_nothrow_move_constructible_v<mdns::browser>);
#endif

static_assert(event_data<mdns::hostname_info>);
static_assert(sizeof(mdns::hostname_info::hostname) == 65);

// Config structs need their required fields.
static_assert(!std::is_default_constructible_v<mdns::service::config>);
static_assert(!std::is_default_constructible_v<mdns::delegated_host::config>);
static_assert(!std::is_default_constructible_v<mdns::query_config>);
static_assert(std::is_default_constructible_v<mdns::lookup_options>);

static_assert(std::same_as<
              decltype(mdns::try_start_query(std::declval<const mdns::query_config&>())),
              result<future<std::vector<mdns::answer>>>>);
static_assert(std::same_as<
              decltype(mdns::try_query(std::declval<const mdns::query_config&>(), mdns::transmission::unicast)),
              result<std::vector<mdns::answer>>>);

// Identity accessors
static_assert(std::same_as<decltype(std::declval<const mdns::delegated_host&>().name()), const std::string&>);
static_assert(std::same_as<decltype(std::declval<const mdns::service&>().type()), const std::string&>);
static_assert(std::same_as<decltype(std::declval<const mdns::service&>().protocol()), mdns::protocol>);
static_assert(std::same_as<decltype(std::declval<const mdns::service&>().instance_name()), const std::optional<std::string>&>);
static_assert(std::same_as<decltype(std::declval<const mdns::service&>().port()), net::port_number>);
static_assert(noexcept(std::declval<const mdns::service&>().port()));

// =============================================================================
// Helpers
// =============================================================================

namespace {

// mdns_init() needs the system default event loop. Create it if no earlier
// test left one running, and destroy only what this test created.
struct mdns_env {
    bool created_loop;

    mdns_env()
        : created_loop(event_loop::try_create_system().has_value()) {
        TEST_ASSERT_TRUE(mdns::try_init().has_value());
    }

    ~mdns_env() {
        mdns::deinit();
        if (created_loop) {
            std::ignore = event_loop::try_destroy_system();
        }
    }
};

// A hostname is required before services can be advertised for the device.
struct mdns_host_env : mdns_env {
    mdns_host_env() { TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value()); }
};

// The answer for the service with this instance name. std::nullopt finds the
// one without an instance name.
const mdns::answer* find_instance(const std::vector<mdns::answer>& answers, std::optional<std::string> instance) {
    auto it = std::ranges::find_if(answers, [&](const mdns::answer& a) { return a.instance_name == instance; });
    return it == answers.end() ? nullptr : &*it;
}

const mdns::txt_item* find_txt(const mdns::answer& a, std::string_view key) {
    auto it = std::ranges::find_if(a.txt, [&](const mdns::txt_item& t) { return t.key == key; });
    return it == a.txt.end() ? nullptr : &*it;
}

size_t free_heap() {
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

constexpr int predefined_netifs = 0
#ifdef CONFIG_MDNS_PREDEF_NETIF_STA
    + 1
#endif
#ifdef CONFIG_MDNS_PREDEF_NETIF_AP
    + 1
#endif
#ifdef CONFIG_MDNS_PREDEF_NETIF_ETH
    + 1
#endif
    ;

} // namespace

// =============================================================================
// Errors and strings
// =============================================================================

TEST_CASE("mdns errc converts to error_code in the mdns category", "[idfxx][mdns]") {
    std::error_code ec = mdns::errc::limit_reached;
    TEST_ASSERT_TRUE(ec.category() == mdns::mdns_category());
    TEST_ASSERT_EQUAL_INT(1, ec.value());
    TEST_ASSERT_EQUAL_STRING("mdns::Error", ec.category().name());
    TEST_ASSERT_EQUAL_STRING("mDNS capacity limit reached", ec.message().c_str());
    TEST_ASSERT_TRUE(ec == mdns::errc::limit_reached);
    TEST_ASSERT_FALSE(ec == idfxx::errc::invalid_state);
}

TEST_CASE("mdns to_string names protocols and record types", "[idfxx][mdns]") {
    TEST_ASSERT_EQUAL_STRING("_tcp", to_string(mdns::protocol::tcp).c_str());
    TEST_ASSERT_EQUAL_STRING("_udp", to_string(mdns::protocol::udp).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(7)", to_string(static_cast<mdns::protocol>(7)).c_str());
    TEST_ASSERT_EQUAL_STRING("A", to_string(mdns::record_type::a).c_str());
    TEST_ASSERT_EQUAL_STRING("PTR", to_string(mdns::record_type::ptr).c_str());
    TEST_ASSERT_EQUAL_STRING("TXT", to_string(mdns::record_type::txt).c_str());
    TEST_ASSERT_EQUAL_STRING("AAAA", to_string(mdns::record_type::aaaa).c_str());
    TEST_ASSERT_EQUAL_STRING("SRV", to_string(mdns::record_type::srv).c_str());
    TEST_ASSERT_EQUAL_STRING("ANY", to_string(mdns::record_type::any).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown(2)", to_string(static_cast<mdns::record_type>(2)).c_str());
#ifdef CONFIG_IDFXX_STD_FORMAT
    TEST_ASSERT_EQUAL_STRING(
        "_udp SRV", std::format("{} {}", mdns::protocol::udp, mdns::record_type::srv).c_str()
    );
#endif
}

TEST_CASE("mdns gives link-local IPv6 answers the zone of their interface", "[idfxx][mdns]") {
    auto zone = [](const char* addr, int index) { return mdns::internal::link_local_zone(net::ipv6_addr::parse(addr)->addr(), index); };
    TEST_ASSERT_EQUAL_UINT8(3, zone("fe80::1", 3));
    TEST_ASSERT_EQUAL_UINT8(255, zone("fe80::1", 255));
    TEST_ASSERT_EQUAL_UINT8(2, zone("febf:ffff::1", 2)); // still fe80::/10
    TEST_ASSERT_EQUAL_UINT8(0, zone("fec0::1", 2));
    TEST_ASSERT_EQUAL_UINT8(0, zone("fd00::1", 2));
    TEST_ASSERT_EQUAL_UINT8(0, zone("2001:db8::1", 2));
    TEST_ASSERT_EQUAL_UINT8(0, zone("80fe::1", 2)); // the bytes, not the word, decide
    // No usable interface index.
    TEST_ASSERT_EQUAL_UINT8(0, zone("fe80::1", 0));
    TEST_ASSERT_EQUAL_UINT8(0, zone("fe80::1", -1));
    TEST_ASSERT_EQUAL_UINT8(0, zone("fe80::1", 256));
}

TEST_CASE("mdns ignores the C library's zone in answers from the network", "[idfxx][mdns]") {
    auto zone = [](const char* addr, uint8_t c_zone, bool arrived, int index) {
        return mdns::internal::answer_zone(net::ipv6_addr::parse(addr)->addr(), c_zone, arrived, index);
    };
    // The C library leaves the zone of a received address uninitialized.
    TEST_ASSERT_EQUAL_UINT8(2, zone("fe80::1", 136, true, 2));
    TEST_ASSERT_EQUAL_UINT8(0, zone("fd0e:9a22::1", 136, true, 2));
    TEST_ASSERT_EQUAL_UINT8(0, zone("fd0e:9a22::1", 2, true, 2));
    TEST_ASSERT_EQUAL_UINT8(2, zone("fe80::1", 0, true, 2));
    // A local lookup keeps the zone that the address was added with.
    TEST_ASSERT_EQUAL_UINT8(5, zone("fe80::1", 5, false, 0));
    TEST_ASSERT_EQUAL_UINT8(0, zone("fd00::1", 0, false, 0));
}

// Queries find nothing without a network, so this checks the step that the
// SRV and TXT query paths apply to their answers.
TEST_CASE("mdns fills SRV and TXT answers with the instance asked for", "[idfxx][mdns]") {
    using mdns::internal::fill_instance_fields;
    const std::optional<std::string> name = "idfxx Printer";
    const std::optional<std::string> type = "_ipp";

    // A TXT-only answer has none of the three. An answer that has some keeps them.
    std::vector<mdns::answer> txt(2);
    txt[1].instance_name = "Other";
    txt[1].protocol = mdns::protocol::udp;
    fill_instance_fields(txt, mdns::record_type::txt, name, type, mdns::protocol::tcp);
    TEST_ASSERT_TRUE(txt[0].instance_name == "idfxx Printer");
    TEST_ASSERT_TRUE(txt[0].service_type == "_ipp");
    TEST_ASSERT_TRUE(txt[0].protocol == mdns::protocol::tcp);
    TEST_ASSERT_TRUE(txt[1].instance_name == "Other");
    TEST_ASSERT_TRUE(txt[1].service_type == "_ipp");
    TEST_ASSERT_TRUE(txt[1].protocol == mdns::protocol::udp);

    std::vector<mdns::answer> srv(1);
    srv[0].service_type = "_http";
    fill_instance_fields(srv, mdns::record_type::srv, name, type, mdns::protocol::tcp);
    TEST_ASSERT_TRUE(srv[0].instance_name == "idfxx Printer");
    TEST_ASSERT_TRUE(srv[0].service_type == "_http");
    TEST_ASSERT_TRUE(srv[0].protocol == mdns::protocol::tcp);

    // Other record types, and queries without a name, are left alone.
    for (auto rtype : {mdns::record_type::ptr, mdns::record_type::a, mdns::record_type::aaaa, mdns::record_type::any}) {
        std::vector<mdns::answer> other(1);
        fill_instance_fields(other, rtype, name, type, mdns::protocol::tcp);
        TEST_ASSERT_TRUE(other[0] == mdns::answer{});
    }
    std::vector<mdns::answer> unnamed(1);
    fill_instance_fields(unnamed, mdns::record_type::txt, std::nullopt, type, mdns::protocol::tcp);
    TEST_ASSERT_TRUE(unnamed[0] == mdns::answer{});
}

// =============================================================================
// Lifecycle
// =============================================================================

TEST_CASE("mdns calls fail with invalid_state before init", "[idfxx][mdns]") {
    mdns::deinit(); // no-op unless an earlier test left it running

    auto r = mdns::try_set_hostname("idfxx-test");
    TEST_ASSERT_FALSE(r.has_value());
    TEST_ASSERT_TRUE(r.error() == idfxx::errc::invalid_state);

    auto h = mdns::try_get_hostname();
    TEST_ASSERT_FALSE(h.has_value());
    TEST_ASSERT_TRUE(h.error() == idfxx::errc::invalid_state);

    auto in = mdns::try_set_instance_name("Test");
    TEST_ASSERT_TRUE(!in && in.error() == idfxx::errc::invalid_state);

    TEST_ASSERT_FALSE(mdns::hostname_exists("idfxx-test"));
    TEST_ASSERT_FALSE(mdns::service_exists("_http", mdns::protocol::tcp));

    auto svc = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!svc && svc.error() == idfxx::errc::invalid_state);

    auto host = mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}});
    TEST_ASSERT_TRUE(!host && host.error() == idfxx::errc::invalid_state);

    auto lk = mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp);
    TEST_ASSERT_TRUE(!lk && lk.error() == idfxx::errc::invalid_state);

    auto q = mdns::try_query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr});
    TEST_ASSERT_TRUE(!q && q.error() == idfxx::errc::invalid_state);

    auto qt = mdns::try_query({.name = "idfxx-peer", .record_type = mdns::record_type::a}, mdns::transmission::multicast);
    TEST_ASSERT_TRUE(!qt && qt.error() == idfxx::errc::invalid_state);

    auto sq = mdns::try_start_query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr});
    TEST_ASSERT_TRUE(!sq && sq.error() == idfxx::errc::invalid_state);

#ifdef CONFIG_LWIP_IPV4
    auto a = mdns::try_query_a("idfxx-peer", 100ms);
    TEST_ASSERT_TRUE(!a && a.error() == idfxx::errc::invalid_state);
#endif

#ifdef CONFIG_MDNS_ENABLE_BROWSE
    auto b = mdns::browser::make({.type = "_http", .protocol = mdns::protocol::tcp}, [](const mdns::answer&) {});
    TEST_ASSERT_TRUE(!b && b.error() == idfxx::errc::invalid_state);
#endif
}

TEST_CASE("mdns init and deinit can be repeated", "[idfxx][mdns]") {
    bool created_loop = event_loop::try_create_system().has_value();
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(mdns::try_init().has_value());
        // A second init while running has no effect.
        TEST_ASSERT_TRUE(mdns::try_init().has_value());
        TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-cycle").has_value());
        auto svc = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
        TEST_ASSERT_TRUE(svc.has_value());
        mdns::deinit();
        // A second deinit does nothing.
        mdns::deinit();
        auto r = mdns::try_set_hostname("idfxx-cycle");
        TEST_ASSERT_TRUE(!r && r.error() == idfxx::errc::invalid_state);
        // The service outlived deinit(). Destroying it must be harmless.
    }
    if (created_loop) {
        std::ignore = event_loop::try_destroy_system();
    }
}

TEST_CASE("mdns objects that outlive deinit do not touch the next session", "[idfxx][mdns]") {
    mdns_host_env env;
    std::optional<mdns::service> stale;
    stale.emplace(mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80}).value());

    mdns::deinit();
    TEST_ASSERT_TRUE(mdns::try_init().has_value());
    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value());
    auto fresh = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(fresh.has_value());

    // Changes through the stale object must not reach the new session's service.
    auto port = stale->try_set_port(1234);
    TEST_ASSERT_TRUE(!port && port.error() == idfxx::errc::invalid_state);
    auto txt = stale->try_set_txt_item({.key = "stale"});
    TEST_ASSERT_TRUE(!txt && txt.error() == idfxx::errc::invalid_state);
    auto sub = stale->try_add_subtype("_stale");
    TEST_ASSERT_TRUE(!sub && sub.error() == idfxx::errc::invalid_state);
    auto name = stale->try_set_instance_name("idfxx Stale");
    TEST_ASSERT_TRUE(!name && name.error() == idfxx::errc::invalid_state);
    auto found = mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp).value();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().port == 80);
    TEST_ASSERT_TRUE(found.front().txt.empty());
    TEST_ASSERT_FALSE(found.front().instance_name.has_value());

    stale.reset(); // must not remove the new session's identical service
    TEST_ASSERT_TRUE(mdns::service_exists("_http", mdns::protocol::tcp));
}

TEST_CASE("mdns delegated hosts that outlive deinit do not touch the next session", "[idfxx][mdns]") {
    mdns_host_env env;
    std::optional<mdns::delegated_host> stale;
    stale.emplace(mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}}).value());

    mdns::deinit();
    TEST_ASSERT_TRUE(mdns::try_init().has_value());
    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value());
    const net::ipv4_addr fresh_addr(10, 0, 0, 3);
    auto fresh = mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {fresh_addr}});
    TEST_ASSERT_TRUE(fresh.has_value());
    auto svc = mdns::service::make(*fresh, {.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(svc.has_value());

    const net::ipv4_addr other[] = {net::ipv4_addr(10, 0, 0, 9)};
    auto r = stale->try_set_addresses(other, {});
    TEST_ASSERT_TRUE(!r && r.error() == idfxx::errc::invalid_state);
    delay(50ms); // a wrongly queued change would have been applied by now
    auto found = mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp).value();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_EQUAL_UINT32(1, found.front().ipv4_addresses.size());
    TEST_ASSERT_TRUE(found.front().ipv4_addresses.front() == fresh_addr);

    stale.reset(); // must not remove the new session's host
    TEST_ASSERT_TRUE(mdns::hostname_exists("idfxx-peer"));
}

// =============================================================================
// Hostname
// =============================================================================

TEST_CASE("mdns hostname can be set, read back and checked", "[idfxx][mdns]") {
    mdns_env env;

    auto none = mdns::try_get_hostname();
    TEST_ASSERT_TRUE(!none && none.error() == idfxx::errc::invalid_state);

    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value());
    auto h = mdns::try_get_hostname();
    TEST_ASSERT_TRUE(h.has_value());
    TEST_ASSERT_EQUAL_STRING("idfxx-test", h->c_str());

    TEST_ASSERT_TRUE(mdns::hostname_exists("idfxx-test"));
    TEST_ASSERT_TRUE(mdns::hostname_exists("IDFXX-TEST"));
    TEST_ASSERT_FALSE(mdns::hostname_exists("someone-else"));
    TEST_ASSERT_FALSE(mdns::hostname_exists(""));
    TEST_ASSERT_FALSE(mdns::hostname_exists(std::string_view("idfxx-test\0x", 12)));

    TEST_ASSERT_TRUE(mdns::try_set_instance_name("idfxx Test Device").has_value());

    auto empty = mdns::try_set_hostname("");
    TEST_ASSERT_TRUE(!empty && empty.error() == idfxx::errc::invalid_arg);
    auto too_long = mdns::try_set_hostname(std::string(64, 'a'));
    TEST_ASSERT_TRUE(!too_long && too_long.error() == idfxx::errc::invalid_arg);
    auto long_instance = mdns::try_set_instance_name(std::string(64, 'i'));
    TEST_ASSERT_TRUE(!long_instance && long_instance.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(mdns::try_set_hostname(std::string(63, 'a')).has_value());
}

TEST_CASE("mdns set_instance_name needs a hostname", "[idfxx][mdns]") {
    mdns_env env;
    auto r = mdns::try_set_instance_name("idfxx Test Device");
    TEST_ASSERT_TRUE(!r && r.error() == idfxx::errc::invalid_state);
}

TEST_CASE("mdns posts hostname_changed to the system event loop", "[idfxx][mdns]") {
    mdns_env env;

    // Static, because a failed assertion leaves the test before the listener
    // is removed, and the listener can still run afterward.
    static std::atomic<int> count;
    static std::string seen;
    count = 0;
    seen.clear();
    auto handle = event_loop::system().try_listener_add(mdns::hostname_changed, [](const mdns::hostname_info& info) {
        seen = info.hostname;
        count++;
    });
    TEST_ASSERT_TRUE(handle.has_value());

    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-evt").has_value());
    for (int i = 0; i < 100 && count.load() == 0; ++i) {
        delay(10ms);
    }
    TEST_ASSERT_EQUAL_INT(1, count.load());
    TEST_ASSERT_EQUAL_STRING("idfxx-evt", seen.c_str());

    // Setting the same hostname again is not a change.
    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-evt").has_value());
    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-evt2").has_value());
    for (int i = 0; i < 100 && count.load() < 2; ++i) {
        delay(10ms);
    }
    delay(50ms);
    TEST_ASSERT_EQUAL_INT(2, count.load());
    TEST_ASSERT_EQUAL_STRING("idfxx-evt2", seen.c_str());

    std::ignore = event_loop::system().try_listener_remove(*handle);
}

// =============================================================================
// Services
// =============================================================================

TEST_CASE("mdns service is advertised and listed by lookup_services", "[idfxx][mdns]") {
    mdns_host_env env;

    const std::string binary("\x00\x01\xff=", 4);
    auto svc = mdns::service::make({
        .instance_name = "idfxx Web",
        .type = "_http",
        .protocol = mdns::protocol::tcp,
        .port = 80,
        .txt = {{.key = "path", .value = "/"}, {.key = "flag"}, {.key = "empty", .value = ""}, {.key = "bin", .value = binary}},
    });
    TEST_ASSERT_TRUE(svc.has_value());

    TEST_ASSERT_TRUE(mdns::service_exists("_http", mdns::protocol::tcp));
    TEST_ASSERT_TRUE(mdns::service_exists("_http", mdns::protocol::tcp, "idfxx Web"));
    TEST_ASSERT_TRUE(mdns::service_exists("_HTTP", mdns::protocol::tcp, "IDFXX WEB"));
    TEST_ASSERT_FALSE(mdns::service_exists("_http", mdns::protocol::tcp, "Another"));
    TEST_ASSERT_FALSE(mdns::service_exists("_http", mdns::protocol::udp));
    TEST_ASSERT_FALSE(mdns::service_exists("_ftp", mdns::protocol::tcp));

    auto found = mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp);
    TEST_ASSERT_TRUE(found.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, found->size());
    const mdns::answer& a = found->front();
    TEST_ASSERT_TRUE(a.instance_name == "idfxx Web");
    TEST_ASSERT_TRUE(a.service_type == "_http");
    TEST_ASSERT_TRUE(a.protocol == mdns::protocol::tcp);
    TEST_ASSERT_TRUE(a.hostname == "idfxx-test");
    TEST_ASSERT_TRUE(a.port == 80);
    TEST_ASSERT_TRUE(a.ipv4_addresses.empty());
    TEST_ASSERT_FALSE(a.netif_key.has_value());
    TEST_ASSERT_FALSE(a.family.has_value());
    TEST_ASSERT_EQUAL_UINT32(4, a.txt.size());
    TEST_ASSERT_NOT_NULL(find_txt(a, "path"));
    TEST_ASSERT_TRUE(find_txt(a, "path")->value == "/");
    TEST_ASSERT_NOT_NULL(find_txt(a, "flag"));
    TEST_ASSERT_FALSE(find_txt(a, "flag")->value.has_value());
    TEST_ASSERT_NOT_NULL(find_txt(a, "empty"));
    TEST_ASSERT_TRUE(find_txt(a, "empty")->value == "");
    TEST_ASSERT_NOT_NULL(find_txt(a, "bin"));
    TEST_ASSERT_TRUE(find_txt(a, "bin")->value == binary);

    // No delegated services.
    auto delegated = mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp);
    TEST_ASSERT_TRUE(delegated.has_value() && delegated->empty());

    // Destroying the service stops advertising it.
    *svc = mdns::service::make({.type = "_ftp", .protocol = mdns::protocol::tcp, .port = 21}).value();
    TEST_ASSERT_FALSE(mdns::service_exists("_http", mdns::protocol::tcp));
    TEST_ASSERT_TRUE(mdns::service_exists("_ftp", mdns::protocol::tcp));
}

TEST_CASE("mdns service port, TXT, subtypes and instance name can be changed", "[idfxx][mdns]") {
    mdns_host_env env;

    auto svc = mdns::service::make({
        .instance_name = "idfxx Printer",
        .type = "_ipp",
        .protocol = mdns::protocol::tcp,
        .port = 631,
        .txt = {{.key = "rp", .value = "ipp/print"}},
        .subtypes = {"_print"},
    });
    TEST_ASSERT_TRUE(svc.has_value());
    TEST_ASSERT_EQUAL_STRING("_ipp", svc->type().c_str());
    TEST_ASSERT_TRUE(svc->protocol() == mdns::protocol::tcp);
    TEST_ASSERT_TRUE(svc->instance_name() == "idfxx Printer");
    TEST_ASSERT_TRUE(svc->port() == 631);

    auto lookup = [](mdns::lookup_options opts = {}) {
        return mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp, opts).value();
    };
    TEST_ASSERT_EQUAL_UINT32(1, lookup({.subtype = "_print"}).size());

    // Port
    TEST_ASSERT_TRUE(svc->try_set_port(8631).has_value());
    TEST_ASSERT_TRUE(lookup().front().port == 8631);
    TEST_ASSERT_TRUE(svc->port() == 8631);
    auto zero = svc->try_set_port(0);
    TEST_ASSERT_TRUE(!zero && zero.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(svc->port() == 8631); // unchanged by a failed call

    // TXT items
    TEST_ASSERT_TRUE(svc->try_set_txt_item({.key = "color", .value = "T"}).has_value());
    TEST_ASSERT_TRUE(svc->try_set_txt_item({.key = "rp", .value = "ipp/fax"}).has_value());
    TEST_ASSERT_TRUE(svc->try_set_txt_item({.key = "duplex"}).has_value());
    auto a = lookup().front();
    TEST_ASSERT_EQUAL_UINT32(3, a.txt.size());
    TEST_ASSERT_TRUE(find_txt(a, "rp")->value == "ipp/fax");
    TEST_ASSERT_TRUE(find_txt(a, "color")->value == "T");
    TEST_ASSERT_FALSE(find_txt(a, "duplex")->value.has_value());

    TEST_ASSERT_TRUE(svc->try_remove_txt_item("color").has_value());
    TEST_ASSERT_NULL(find_txt(lookup().front(), "color"));
    // Removing a missing item does nothing.
    TEST_ASSERT_TRUE(svc->try_remove_txt_item("color").has_value());
    // A key with a NUL is rejected, not cut short to "rp".
    auto nul_key = svc->try_remove_txt_item(std::string_view("rp\0x", 4));
    TEST_ASSERT_TRUE(!nul_key && nul_key.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_NOT_NULL(find_txt(lookup().front(), "rp"));
    auto missing_sub = svc->try_remove_subtype("_none");
    TEST_ASSERT_TRUE(!missing_sub && missing_sub.error() == idfxx::errc::not_found);

    const std::vector<mdns::txt_item> txt = {{.key = "a", .value = "1"}, {.key = "b"}};
    TEST_ASSERT_TRUE(svc->try_set_txt(txt).has_value());
    a = lookup().front();
    TEST_ASSERT_EQUAL_UINT32(2, a.txt.size());
    TEST_ASSERT_TRUE(find_txt(a, "a")->value == "1");
    TEST_ASSERT_FALSE(find_txt(a, "b")->value.has_value());
    TEST_ASSERT_TRUE(svc->try_set_txt({}).has_value());
    TEST_ASSERT_TRUE(lookup().front().txt.empty());

    auto bad_key = svc->try_set_txt_item({.key = "a=b", .value = "c"});
    TEST_ASSERT_TRUE(!bad_key && bad_key.error() == idfxx::errc::invalid_arg);
    auto too_big = svc->try_set_txt_item({.key = "k", .value = std::string(254, 'x')});
    TEST_ASSERT_TRUE(!too_big && too_big.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(svc->try_set_txt_item({.key = "k", .value = std::string(253, 'x')}).has_value());

    // Subtypes
    TEST_ASSERT_TRUE(svc->try_add_subtype("_scan").has_value());
    TEST_ASSERT_EQUAL_UINT32(1, lookup({.subtype = "_scan"}).size());
    TEST_ASSERT_TRUE(svc->try_remove_subtype("_print").has_value());
    TEST_ASSERT_TRUE(lookup({.subtype = "_print"}).empty());
    const std::vector<std::string> subtypes = {"_fax", "_copy"};
    TEST_ASSERT_TRUE(svc->try_set_subtypes(subtypes).has_value());
    TEST_ASSERT_TRUE(lookup({.subtype = "_scan"}).empty());
    TEST_ASSERT_EQUAL_UINT32(1, lookup({.subtype = "_fax"}).size());
    TEST_ASSERT_EQUAL_UINT32(1, lookup({.subtype = "_copy"}).size());
    TEST_ASSERT_TRUE(svc->try_set_subtypes({}).has_value());
    TEST_ASSERT_TRUE(lookup({.subtype = "_fax"}).empty());

    // Instance name
    TEST_ASSERT_TRUE(svc->try_set_instance_name("idfxx Renamed").has_value());
    TEST_ASSERT_TRUE(lookup().front().instance_name == "idfxx Renamed");
    TEST_ASSERT_TRUE(svc->instance_name() == "idfxx Renamed");
    TEST_ASSERT_TRUE(mdns::service_exists("_ipp", mdns::protocol::tcp, "idfxx Renamed"));
    TEST_ASSERT_FALSE(mdns::service_exists("_ipp", mdns::protocol::tcp, "idfxx Printer"));
    TEST_ASSERT_EQUAL_UINT32(1, lookup({.instance_name = "idfxx Renamed"}).size());
    // Later calls follow the new name.
    TEST_ASSERT_TRUE(svc->try_set_port(631).has_value());
    TEST_ASSERT_TRUE(lookup().front().port == 631);
    TEST_ASSERT_TRUE(svc->port() == 631);
}

TEST_CASE("mdns service without an instance name reports none until renamed", "[idfxx][mdns]") {
    mdns_host_env env;

    auto svc = mdns::service::make({.type = "_ssh", .protocol = mdns::protocol::udp, .port = 22});
    TEST_ASSERT_TRUE(svc.has_value());
    TEST_ASSERT_FALSE(svc->instance_name().has_value());
    TEST_ASSERT_EQUAL_STRING("_ssh", svc->type().c_str());
    TEST_ASSERT_TRUE(svc->protocol() == mdns::protocol::udp);
    TEST_ASSERT_TRUE(svc->port() == 22);

    auto bad = svc->try_set_instance_name("");
    TEST_ASSERT_TRUE(!bad && bad.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_FALSE(svc->instance_name().has_value());

    TEST_ASSERT_TRUE(svc->try_set_instance_name("idfxx Shell").has_value());
    TEST_ASSERT_TRUE(svc->instance_name() == "idfxx Shell");

    // The accessors follow the object when it moves.
    mdns::service moved = std::move(*svc);
    TEST_ASSERT_TRUE(moved.instance_name() == "idfxx Shell");
    TEST_ASSERT_TRUE(moved.port() == 22);
}

TEST_CASE("mdns service without an instance name must be alone of its type", "[idfxx][mdns]") {
    mdns_host_env env;
    auto lookup = [] { return mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp).value(); };

    // Unnamed first: a named one of the same type is refused...
    std::optional<mdns::service> unnamed;
    unnamed.emplace(mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80}).value());
    auto named = mdns::service::make({.instance_name = "idfxx Named", .type = "_HTTP", .protocol = mdns::protocol::tcp, .port = 81});
    TEST_ASSERT_TRUE(!named && named.error() == idfxx::errc::invalid_state);
    auto unnamed2 = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 82});
    TEST_ASSERT_TRUE(!unnamed2 && unnamed2.error() == idfxx::errc::invalid_state);
    // ...and the unnamed one is untouched.
    auto found = lookup();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().port == 80);
    // Another protocol is another key.
    TEST_ASSERT_TRUE(mdns::service::make({.instance_name = "idfxx Udp", .type = "_http", .protocol = mdns::protocol::udp, .port = 83}).has_value());

    // Once named, it can share its type.
    TEST_ASSERT_TRUE(unnamed->try_set_instance_name("idfxx First").has_value());
    std::optional<mdns::service> second;
    second.emplace(mdns::service::make({.instance_name = "idfxx Second", .type = "_http", .protocol = mdns::protocol::tcp, .port = 84}).value());
    // A rename can't take another service's name.
    auto clash = second->try_set_instance_name("IDFXX FIRST");
    TEST_ASSERT_TRUE(!clash && clash.error() == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(second->instance_name() == "idfxx Second");
    second.reset();
    unnamed.reset();
    TEST_ASSERT_TRUE(lookup().empty());

    // Named first: an unnamed one of the same type is refused, and the named one is untouched.
    std::optional<mdns::service> first;
    first.emplace(mdns::service::make({.instance_name = "idfxx Named", .type = "_http", .protocol = mdns::protocol::tcp, .port = 85}).value());
    auto late = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 86});
    TEST_ASSERT_TRUE(!late && late.error() == idfxx::errc::invalid_state);
    auto same_name = mdns::service::make({.instance_name = "IDFXX NAMED", .type = "_http", .protocol = mdns::protocol::tcp, .port = 87});
    TEST_ASSERT_TRUE(!same_name && same_name.error() == idfxx::errc::invalid_state);
    found = lookup();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().port == 85);
    TEST_ASSERT_TRUE(first->try_set_port(88).has_value());
    TEST_ASSERT_TRUE(lookup().front().port == 88);

    // Once it is gone, an unnamed one may take the type.
    first.reset();
    TEST_ASSERT_TRUE(mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 89}).has_value());
}

TEST_CASE("mdns named services of one type are changed and removed apart", "[idfxx][mdns]") {
    mdns_host_env env;
    auto lookup = [] { return mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp).value(); };

    std::optional<mdns::service> a;
    a.emplace(mdns::service::make({.instance_name = "idfxx A", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 631}).value());
    std::optional<mdns::service> b;
    b.emplace(mdns::service::make({.instance_name = "idfxx B", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 632}).value());

    TEST_ASSERT_TRUE(a->try_set_port(1631).has_value());
    TEST_ASSERT_TRUE(b->try_set_txt(std::vector<mdns::txt_item>{{.key = "who", .value = "b"}}).has_value());
    TEST_ASSERT_TRUE(a->try_set_txt_item({.key = "who", .value = "a"}).has_value());
    TEST_ASSERT_TRUE(b->try_add_subtype("_b").has_value());
    auto found = lookup();
    TEST_ASSERT_EQUAL_UINT32(2, found.size());
    const mdns::answer* fa = find_instance(found, "idfxx A");
    const mdns::answer* fb = find_instance(found, "idfxx B");
    TEST_ASSERT_NOT_NULL(fa);
    TEST_ASSERT_NOT_NULL(fb);
    TEST_ASSERT_TRUE(fa->port == 1631);
    TEST_ASSERT_TRUE(fb->port == 632);
    TEST_ASSERT_EQUAL_UINT32(1, fa->txt.size());
    TEST_ASSERT_TRUE(find_txt(*fa, "who")->value == "a");
    TEST_ASSERT_EQUAL_UINT32(1, fb->txt.size());
    TEST_ASSERT_TRUE(find_txt(*fb, "who")->value == "b");
    auto with_b = mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp, {.subtype = "_b"}).value();
    TEST_ASSERT_EQUAL_UINT32(1, with_b.size());
    TEST_ASSERT_TRUE(with_b.front().instance_name == "idfxx B");
    // A has no "_b" to remove, whatever B has.
    auto missing = a->try_remove_subtype("_b");
    TEST_ASSERT_TRUE(!missing && missing.error() == idfxx::errc::not_found);

    // Destroying A removes only A...
    a.reset();
    found = lookup();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().instance_name == "idfxx B");
    TEST_ASSERT_TRUE(found.front().port == 632);

    // ...and B, now alone of its type, still works.
    TEST_ASSERT_TRUE(b->try_set_port(2632).has_value());
    TEST_ASSERT_TRUE(b->try_set_txt_item({.key = "alone"}).has_value());
    TEST_ASSERT_TRUE(b->try_remove_subtype("_b").has_value());
    TEST_ASSERT_TRUE(b->try_set_instance_name("idfxx B2").has_value());
    TEST_ASSERT_TRUE(b->try_remove_txt_item("who").has_value());
    found = lookup();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().instance_name == "idfxx B2");
    TEST_ASSERT_TRUE(found.front().port == 2632);
    TEST_ASSERT_NOT_NULL(find_txt(found.front(), "alone"));
    TEST_ASSERT_NULL(find_txt(found.front(), "who"));
    b.reset();
    TEST_ASSERT_TRUE(lookup().empty());
}

TEST_CASE("mdns services alone of their type follow instance name and hostname changes", "[idfxx][mdns]") {
    mdns_host_env env;

    std::optional<mdns::service> unnamed;
    unnamed.emplace(mdns::service::make({.type = "_ipp", .protocol = mdns::protocol::tcp, .port = 631}).value());
    std::optional<mdns::service> named;
    named.emplace(mdns::service::make({.instance_name = "idfxx Web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 80}).value());
    auto ipp = [] { return mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp).value(); };
    auto http = [] { return mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp).value(); };

    auto exercise = [&](net::port_number port) {
        const auto port2 = static_cast<net::port_number>(port + 1);
        TEST_ASSERT_TRUE(unnamed->try_set_port(port).has_value());
        TEST_ASSERT_TRUE(named->try_set_port(port2).has_value());
        TEST_ASSERT_TRUE(unnamed->try_set_txt_item({.key = "p", .value = std::to_string(port)}).has_value());
        TEST_ASSERT_TRUE(named->try_set_txt_item({.key = "p", .value = std::to_string(port)}).has_value());
        // The unnamed service can't remove a subtype, so it adds a new one each time.
        const std::string sub = "_s" + std::to_string(port);
        TEST_ASSERT_TRUE(unnamed->try_add_subtype(sub).has_value());
        TEST_ASSERT_TRUE(named->try_add_subtype("_s").has_value());
        TEST_ASSERT_EQUAL_UINT32(1, mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp, {.subtype = sub}).value().size());
        TEST_ASSERT_EQUAL_UINT32(1, mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp, {.subtype = "_s"}).value().size());
        auto kept = unnamed->try_remove_subtype(sub);
        TEST_ASSERT_TRUE(!kept && kept.error() == idfxx::errc::not_supported);
        TEST_ASSERT_TRUE(named->try_remove_subtype("_s").has_value());
        TEST_ASSERT_EQUAL_UINT32(1, mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp, {.subtype = sub}).value().size());
        TEST_ASSERT_TRUE(mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp, {.subtype = "_s"}).value().empty());
        auto u = ipp();
        auto n = http();
        TEST_ASSERT_EQUAL_UINT32(1, u.size());
        TEST_ASSERT_EQUAL_UINT32(1, n.size());
        TEST_ASSERT_TRUE(u.front().port == port);
        TEST_ASSERT_TRUE(n.front().port == port2);
        TEST_ASSERT_TRUE(find_txt(u.front(), "p")->value == std::to_string(port));
        TEST_ASSERT_TRUE(find_txt(n.front(), "p")->value == std::to_string(port));
    };

    exercise(1000);
    TEST_ASSERT_TRUE(mdns::try_set_instance_name("idfxx Default").has_value());
    // At once, when the mDNS task may not have applied the new name yet...
    exercise(2000);
    // ...and once it has.
    delay(50ms);
    exercise(3000);
    TEST_ASSERT_FALSE(unnamed->instance_name().has_value());

    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test2").has_value());
    exercise(4000);
    TEST_ASSERT_TRUE(ipp().front().hostname == "idfxx-test2");

    unnamed.reset();
    named.reset();
    TEST_ASSERT_TRUE(ipp().empty());
    TEST_ASSERT_TRUE(http().empty());
}

TEST_CASE("mdns service without an instance name only keeps or adds subtypes", "[idfxx][mdns]") {
    mdns_host_env env;
    auto with = [](const char* subtype) {
        return mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp, {.subtype = subtype}).value().size();
    };

    std::optional<mdns::service> svc;
    svc.emplace(mdns::service::make({.type = "_ipp", .protocol = mdns::protocol::tcp, .port = 631, .subtypes = {"_print"}}).value());
    TEST_ASSERT_EQUAL_UINT32(1, with("_print"));

    // Removing a subtype is refused, whether or not the service has it.
    auto remove = svc->try_remove_subtype("_print");
    TEST_ASSERT_TRUE(!remove && remove.error() == idfxx::errc::not_supported);
    auto missing = svc->try_remove_subtype("_none");
    TEST_ASSERT_TRUE(!missing && missing.error() == idfxx::errc::not_supported);
    TEST_ASSERT_EQUAL_UINT32(1, with("_print"));

    // So is a set that leaves one out, also when only the case differs.
    const std::vector<std::string> other = {"_scan"};
    auto drop = svc->try_set_subtypes(other);
    TEST_ASSERT_TRUE(!drop && drop.error() == idfxx::errc::not_supported);
    const std::vector<std::string> recased = {"_PRINT"};
    auto drop_case = svc->try_set_subtypes(recased);
    TEST_ASSERT_TRUE(!drop_case && drop_case.error() == idfxx::errc::not_supported);
    auto clear = svc->try_set_subtypes({});
    TEST_ASSERT_TRUE(!clear && clear.error() == idfxx::errc::not_supported);
    // The refused calls changed nothing, so the same set is refused again.
    auto drop_again = svc->try_set_subtypes(other);
    TEST_ASSERT_TRUE(!drop_again && drop_again.error() == idfxx::errc::not_supported);
    TEST_ASSERT_EQUAL_UINT32(1, with("_print"));
    TEST_ASSERT_EQUAL_UINT32(0, with("_scan"));

    // A set that keeps every subtype adds the new ones.
    const std::vector<std::string> more = {"_scan", "_print"};
    TEST_ASSERT_TRUE(svc->try_set_subtypes(more).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, with("_print"));
    TEST_ASSERT_EQUAL_UINT32(1, with("_scan"));
    TEST_ASSERT_TRUE(svc->try_set_subtypes(more).has_value()); // the same list again

    // A subtype added on its own must be kept too.
    TEST_ASSERT_TRUE(svc->try_add_subtype("_fax").has_value());
    auto drop_added = svc->try_set_subtypes(more);
    TEST_ASSERT_TRUE(!drop_added && drop_added.error() == idfxx::errc::not_supported);
    TEST_ASSERT_EQUAL_UINT32(1, with("_fax"));
    const std::vector<std::string> all = {"_print", "_fax", "_scan", "_copy"};
    TEST_ASSERT_TRUE(svc->try_set_subtypes(all).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, with("_copy"));

    // Once named, the service can remove them.
    TEST_ASSERT_TRUE(svc->try_set_instance_name("idfxx Printer").has_value());
    TEST_ASSERT_TRUE(svc->try_remove_subtype("_fax").has_value());
    TEST_ASSERT_EQUAL_UINT32(0, with("_fax"));
    TEST_ASSERT_TRUE(svc->try_set_subtypes({}).has_value());
    TEST_ASSERT_EQUAL_UINT32(0, with("_print"));
    TEST_ASSERT_EQUAL_UINT32(0, with("_copy"));
}

TEST_CASE("mdns named service alone of its type removes subtypes", "[idfxx][mdns]") {
    mdns_host_env env;
    auto with = [](const char* subtype) {
        return mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp, {.subtype = subtype}).value().size();
    };

    auto svc = mdns::service::make({.instance_name = "idfxx Printer", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 631, .subtypes = {"_print", "_scan"}});
    TEST_ASSERT_TRUE(svc.has_value());

    // Subtypes that differ only in case are different.
    auto recased = svc->try_remove_subtype("_PRINT");
    TEST_ASSERT_TRUE(!recased && recased.error() == idfxx::errc::not_found);
    TEST_ASSERT_EQUAL_UINT32(1, with("_print"));

    TEST_ASSERT_TRUE(svc->try_remove_subtype("_print").has_value());
    TEST_ASSERT_EQUAL_UINT32(0, with("_print"));
    TEST_ASSERT_EQUAL_UINT32(1, with("_scan"));
    auto again = svc->try_remove_subtype("_print");
    TEST_ASSERT_TRUE(!again && again.error() == idfxx::errc::not_found);

    const std::vector<std::string> replaced = {"_fax"};
    TEST_ASSERT_TRUE(svc->try_set_subtypes(replaced).has_value());
    TEST_ASSERT_EQUAL_UINT32(0, with("_scan"));
    TEST_ASSERT_EQUAL_UINT32(1, with("_fax"));
}

TEST_CASE("mdns subtype lists can't hold the same subtype twice", "[idfxx][mdns]") {
    mdns_host_env env;
    auto with = [](const char* subtype) {
        return mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp, {.subtype = subtype}).value().size();
    };

    auto dup_cfg = mdns::service::make({.instance_name = "idfxx Web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .subtypes = {"_a", "_b", "_a"}});
    TEST_ASSERT_TRUE(!dup_cfg && dup_cfg.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_FALSE(mdns::service_exists("_http", mdns::protocol::tcp));

    // Subtypes that differ only in case are different.
    std::optional<mdns::service> named;
    named.emplace(mdns::service::make({.instance_name = "idfxx Web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .subtypes = {"_a", "_A"}}).value());
    const std::vector<std::string> dup = {"_c", "_d", "_c"};
    auto set_dup = named->try_set_subtypes(dup);
    TEST_ASSERT_TRUE(!set_dup && set_dup.error() == idfxx::errc::invalid_arg);
    // The refused list changed nothing.
    TEST_ASSERT_EQUAL_UINT32(1, with("_a"));
    TEST_ASSERT_EQUAL_UINT32(0, with("_c"));
    TEST_ASSERT_EQUAL_UINT32(0, with("_d"));
    named.reset();

    // Without an instance name, the service still has its subtype after a
    // refused list, so a set that leaves it out is refused too.
    auto unnamed = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .subtypes = {"_a"}});
    TEST_ASSERT_TRUE(unnamed.has_value());
    const std::vector<std::string> dup_b = {"_b", "_b"};
    auto unnamed_dup = unnamed->try_set_subtypes(dup_b);
    TEST_ASSERT_TRUE(!unnamed_dup && unnamed_dup.error() == idfxx::errc::invalid_arg);
    const std::vector<std::string> only_b = {"_b"};
    auto drop = unnamed->try_set_subtypes(only_b);
    TEST_ASSERT_TRUE(!drop && drop.error() == idfxx::errc::not_supported);
    TEST_ASSERT_EQUAL_UINT32(1, with("_a"));
    TEST_ASSERT_EQUAL_UINT32(0, with("_b"));
}

TEST_CASE("mdns device and delegated hosts are separate for the instance name rule", "[idfxx][mdns]") {
    mdns_host_env env;
    auto self_lookup = [](mdns::lookup_options o = {}) { return mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp, o).value(); };
    auto peer_lookup = [](mdns::lookup_options o = {}) { return mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp, o).value(); };

    auto host = mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(192, 168, 7, 50)}});
    TEST_ASSERT_TRUE(host.has_value());
    // An unnamed service on each host.
    auto own = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(own.has_value());
    auto peer = mdns::service::make(*host, {.type = "_http", .protocol = mdns::protocol::tcp, .port = 8080});
    TEST_ASSERT_TRUE(peer.has_value());

    TEST_ASSERT_TRUE(own->try_set_port(81).has_value());
    TEST_ASSERT_TRUE(peer->try_set_port(8081).has_value());
    TEST_ASSERT_TRUE(self_lookup().front().port == 81);
    TEST_ASSERT_TRUE(peer_lookup().front().port == 8081);
}

TEST_CASE("mdns remove_subtype on the device leaves a delegated host's same-named service alone", "[idfxx][mdns]") {
    mdns_host_env env;
    auto peer_with = [](const char* subtype) {
        return mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp, {.subtype = subtype}).value().size();
    };

    auto host = mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(192, 168, 7, 50)}});
    TEST_ASSERT_TRUE(host.has_value());
    // Created first, so the C library lists it last.
    auto own = mdns::service::make({.instance_name = "web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(own.has_value());
    auto peer = mdns::service::make(*host, {.instance_name = "web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 8080, .subtypes = {"_x"}});
    TEST_ASSERT_TRUE(peer.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, peer_with("_x"));

    auto missing = own->try_remove_subtype("_x");
    TEST_ASSERT_TRUE(!missing && missing.error() == idfxx::errc::not_found);
    TEST_ASSERT_EQUAL_UINT32(1, peer_with("_x"));
}

TEST_CASE("mdns service on a delegated host from before deinit is refused", "[idfxx][mdns]") {
    mdns_host_env env;
    std::optional<mdns::delegated_host> stale;
    stale.emplace(mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}}).value());

    mdns::deinit();
    TEST_ASSERT_TRUE(mdns::try_init().has_value());
    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value());

    auto svc = mdns::service::make(*stale, {.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!svc && svc.error() == idfxx::errc::invalid_state);
    TEST_ASSERT_TRUE(mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp).value().empty());
}

TEST_CASE("mdns service that outlives its delegated host touches no other service", "[idfxx][mdns]") {
    mdns_host_env env;
    std::optional<mdns::delegated_host> host;
    host.emplace(mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}}).value());
    std::optional<mdns::service> orphan;
    orphan.emplace(mdns::service::make(*host, {.type = "_http", .protocol = mdns::protocol::tcp, .port = 80}).value());

    // Against the contract: the host goes first, taking its services with it.
    host.reset();
    host.emplace(mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 3)}}).value());
    auto fresh = mdns::service::make(*host, {.type = "_http", .protocol = mdns::protocol::tcp, .port = 9000});
    TEST_ASSERT_TRUE(fresh.has_value());

    auto r = orphan->try_set_port(1);
    TEST_ASSERT_TRUE(!r && r.error() == idfxx::errc::not_found);
    orphan.reset();
    auto found = mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp).value();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().port == 9000);
}

TEST_CASE("mdns service renamed by the network while it shares its type is not mistaken for another", "[idfxx][mdns]") {
    mdns_host_env env;
    auto lookup = [] { return mdns::try_lookup_services(mdns::self_hosted, "_ipp", mdns::protocol::tcp).value(); };

    // Bar first, then Foo: the mDNS library lists Foo first, so a call
    // without an instance name would find Foo.
    std::optional<mdns::service> bar;
    bar.emplace(mdns::service::make({.instance_name = "idfxx Bar", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 631}).value());
    std::optional<mdns::service> foo;
    foo.emplace(mdns::service::make({.instance_name = "idfxx Foo", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 632}).value());

    // Rename Foo in the mDNS library, as it does when another host claims the name.
    TEST_ASSERT_EQUAL_INT(ESP_OK, mdns_service_instance_name_set_for_host("idfxx Foo", "_ipp", "_tcp", nullptr, "idfxx Foo 2"));
    // Foo is no longer found, so it stays advertised.
    foo.reset();
    TEST_ASSERT_EQUAL_UINT32(2, lookup().size());

    // Bar still changes only itself.
    TEST_ASSERT_TRUE(bar->try_set_port(9).has_value());
    TEST_ASSERT_TRUE(bar->try_set_txt_item({.key = "who", .value = "bar"}).has_value());
    auto found = lookup();
    const mdns::answer* fb = find_instance(found, "idfxx Bar");
    const mdns::answer* ff = find_instance(found, "idfxx Foo 2");
    TEST_ASSERT_NOT_NULL(fb);
    TEST_ASSERT_NOT_NULL(ff);
    TEST_ASSERT_TRUE(fb->port == 9);
    TEST_ASSERT_NOT_NULL(find_txt(*fb, "who"));
    TEST_ASSERT_TRUE(ff->port == 632);
    TEST_ASSERT_NULL(find_txt(*ff, "who"));

    // The stranded service blocks a service without an instance name of its type, but not a named one.
    auto unnamed = mdns::service::make({.type = "_ipp", .protocol = mdns::protocol::tcp, .port = 633});
    TEST_ASSERT_TRUE(!unnamed && unnamed.error() == idfxx::errc::invalid_state);
    auto named = mdns::service::make({.instance_name = "idfxx Baz", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 634});
    TEST_ASSERT_TRUE(named.has_value());
    named = idfxx::error(idfxx::errc::fail); // destroys Baz
    TEST_ASSERT_EQUAL_UINT32(2, lookup().size());

    // Destroying Bar removes Bar, not Foo.
    bar.reset();
    found = lookup();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().instance_name == "idfxx Foo 2");

    // deinit() removes the stranded service, and forgets it.
    mdns::deinit();
    TEST_ASSERT_TRUE(mdns::try_init().has_value());
    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value());
    TEST_ASSERT_TRUE(mdns::service::make({.type = "_ipp", .protocol = mdns::protocol::tcp, .port = 635}).has_value());
}

TEST_CASE("mdns delegated host takes its stranded services with it", "[idfxx][mdns]") {
    mdns_host_env env;
    auto lookup = [] { return mdns::try_lookup_services(mdns::delegated, "_ipp", mdns::protocol::tcp).value(); };
    const mdns::delegated_host::config host_cfg{.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}};

    std::optional<mdns::delegated_host> host;
    host.emplace(mdns::delegated_host::make(host_cfg).value());
    std::optional<mdns::service> bar;
    bar.emplace(mdns::service::make(*host, {.instance_name = "idfxx Bar", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 631}).value());
    std::optional<mdns::service> foo;
    foo.emplace(mdns::service::make(*host, {.instance_name = "idfxx Foo", .type = "_ipp", .protocol = mdns::protocol::tcp, .port = 632}).value());

    TEST_ASSERT_EQUAL_INT(ESP_OK, mdns_service_instance_name_set_for_host("idfxx Foo", "_ipp", "_tcp", "idfxx-peer", "idfxx Foo 2"));
    foo.reset();
    TEST_ASSERT_TRUE(bar->try_set_port(9).has_value());
    auto found = lookup();
    const mdns::answer* fb = find_instance(found, "idfxx Bar");
    TEST_ASSERT_NOT_NULL(fb);
    TEST_ASSERT_TRUE(fb->port == 9);
    bar.reset();
    TEST_ASSERT_EQUAL_UINT32(1, lookup().size());

    // The host's removal takes the stranded service with it, so a service
    // without an instance name can take the type on a new host of that name.
    host.reset();
    host.emplace(mdns::delegated_host::make(host_cfg).value());
    TEST_ASSERT_TRUE(lookup().empty());
    TEST_ASSERT_TRUE(mdns::service::make(*host, {.type = "_ipp", .protocol = mdns::protocol::tcp, .port = 633}).has_value());
}

TEST_CASE("mdns service on a delegated host needs the device's hostname", "[idfxx][mdns]") {
    mdns_env env;
    auto host = mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}});
    TEST_ASSERT_TRUE(host.has_value());

    auto svc = mdns::service::make(*host, {.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!svc && svc.error() == idfxx::errc::invalid_state);

    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value());
    TEST_ASSERT_TRUE(mdns::service::make(*host, {.type = "_http", .protocol = mdns::protocol::tcp, .port = 80}).has_value());
}

TEST_CASE("mdns service names are limited to what the mDNS library stores", "[idfxx][mdns]") {
    mdns_host_env env;

    // One DNS label each: at most 63 characters.
    const std::string type63 = "_" + std::string(62, 't');
    const std::string name63(63, 'n');
    const std::string sub63 = "_" + std::string(62, 's');

    auto long_type = mdns::service::make({.type = type63 + "x", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!long_type && long_type.error() == idfxx::errc::invalid_arg);
    auto long_name = mdns::service::make({.instance_name = name63 + "x", .type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!long_name && long_name.error() == idfxx::errc::invalid_arg);
    auto long_sub = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .subtypes = {sub63 + "x"}});
    TEST_ASSERT_TRUE(!long_sub && long_sub.error() == idfxx::errc::invalid_arg);

    // At the limits, the service can still be changed and found.
    auto svc = mdns::service::make({.instance_name = name63, .type = type63, .protocol = mdns::protocol::tcp, .port = 80, .subtypes = {sub63}});
    TEST_ASSERT_TRUE(svc.has_value());
    TEST_ASSERT_TRUE(svc->try_set_port(81).has_value());
    auto found = mdns::try_lookup_services(mdns::self_hosted, type63, mdns::protocol::tcp, {.subtype = sub63}).value();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_TRUE(found.front().port == 81);
    TEST_ASSERT_TRUE(mdns::service_exists(type63, mdns::protocol::tcp, name63));

    auto add_long = svc->try_add_subtype(sub63 + "x");
    TEST_ASSERT_TRUE(!add_long && add_long.error() == idfxx::errc::invalid_arg);
    auto remove_long = svc->try_remove_subtype(sub63 + "x");
    TEST_ASSERT_TRUE(!remove_long && remove_long.error() == idfxx::errc::invalid_arg);
    const std::vector<std::string> long_subs = {sub63 + "x"};
    auto set_long = svc->try_set_subtypes(long_subs);
    TEST_ASSERT_TRUE(!set_long && set_long.error() == idfxx::errc::invalid_arg);
    auto rename_long = svc->try_set_instance_name(name63 + "x");
    TEST_ASSERT_TRUE(!rename_long && rename_long.error() == idfxx::errc::invalid_arg);
    // A TXT key has no limit of its own beyond the 255-byte item.
    TEST_ASSERT_TRUE(svc->try_set_txt_item({.key = std::string(255, 'k')}).has_value());

    // Lookups and checks reject names no service can have.
    auto lk_type = mdns::try_lookup_services(mdns::self_hosted, type63 + "x", mdns::protocol::tcp);
    TEST_ASSERT_TRUE(!lk_type && lk_type.error() == idfxx::errc::invalid_arg);
    auto lk_name = mdns::try_lookup_services(mdns::self_hosted, type63, mdns::protocol::tcp, {.instance_name = name63 + "x"});
    TEST_ASSERT_TRUE(!lk_name && lk_name.error() == idfxx::errc::invalid_arg);
    auto lk_sub = mdns::try_lookup_services(mdns::delegated, type63, mdns::protocol::tcp, {.subtype = sub63 + "x"});
    TEST_ASSERT_TRUE(!lk_sub && lk_sub.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_FALSE(mdns::service_exists(type63 + "x", mdns::protocol::tcp));
    TEST_ASSERT_FALSE(mdns::service_exists(type63, mdns::protocol::tcp, name63 + "x"));

    auto host = mdns::delegated_host::make({.name = std::string(64, 'h'), .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}});
    TEST_ASSERT_TRUE(!host && host.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_TRUE(mdns::delegated_host::make({.name = std::string(63, 'h'), .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}}).has_value());
}

TEST_CASE("mdns lookup_services honors max_results", "[idfxx][mdns]") {
    mdns_host_env env;
    auto one = mdns::service::make({.instance_name = "one", .type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    auto two = mdns::service::make({.instance_name = "two", .type = "_http", .protocol = mdns::protocol::tcp, .port = 81});
    TEST_ASSERT_TRUE(one.has_value() && two.has_value());

    auto all = mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp);
    TEST_ASSERT_EQUAL_UINT32(2, all.value().size());
    auto limited = mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp, {.max_results = 1});
    TEST_ASSERT_EQUAL_UINT32(1, limited.value().size());
    auto zero = mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp, {.max_results = 0});
    TEST_ASSERT_TRUE(!zero && zero.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("mdns service rejects invalid configurations", "[idfxx][mdns]") {
    mdns_env env;

    // No hostname yet
    auto no_host = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!no_host && no_host.error() == idfxx::errc::invalid_state);

    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-test").has_value());

    auto port0 = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 0});
    TEST_ASSERT_TRUE(!port0 && port0.error() == idfxx::errc::invalid_arg);
    auto no_type = mdns::service::make({.type = "", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!no_type && no_type.error() == idfxx::errc::invalid_arg);
    auto bad_txt = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .txt = {{.key = ""}}});
    TEST_ASSERT_TRUE(!bad_txt && bad_txt.error() == idfxx::errc::invalid_arg);
    auto bad_sub = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .subtypes = {""}});
    TEST_ASSERT_TRUE(!bad_sub && bad_sub.error() == idfxx::errc::invalid_arg);

    // The mDNS library would cut a name short at an embedded NUL.
    const std::string nul_name("idfxx\0Web", 9);
    const std::string nul_sub("_a\0b", 4);
    auto nul_type = mdns::service::make({.type = std::string("_ht\0tp", 6), .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!nul_type && nul_type.error() == idfxx::errc::invalid_arg);
    auto nul_instance = mdns::service::make({.instance_name = nul_name, .type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(!nul_instance && nul_instance.error() == idfxx::errc::invalid_arg);
    auto nul_subtype = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .subtypes = {nul_sub}});
    TEST_ASSERT_TRUE(!nul_subtype && nul_subtype.error() == idfxx::errc::invalid_arg);
    auto nul_host = mdns::delegated_host::make({.name = std::string("idfxx\0peer", 10), .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}});
    TEST_ASSERT_TRUE(!nul_host && nul_host.error() == idfxx::errc::invalid_arg);
    auto nul_hostname = mdns::try_set_hostname(std::string("idfxx\0test", 10));
    TEST_ASSERT_TRUE(!nul_hostname && nul_hostname.error() == idfxx::errc::invalid_arg);
    TEST_ASSERT_FALSE(mdns::service_exists("_http", mdns::protocol::tcp));

    auto first = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(first.has_value());
    auto dup = mdns::service::make({.type = "_http", .protocol = mdns::protocol::tcp, .port = 8080});
    TEST_ASSERT_TRUE(!dup && dup.error() == idfxx::errc::invalid_state);
}

TEST_CASE("mdns service fails with limit_reached beyond CONFIG_MDNS_MAX_SERVICES", "[idfxx][mdns]") {
    mdns_host_env env;

    std::vector<mdns::service> services;
    for (int i = 0; i < CONFIG_MDNS_MAX_SERVICES; ++i) {
        auto svc = mdns::service::make({
            .type = "_svc" + std::to_string(i),
            .protocol = mdns::protocol::tcp,
            .port = static_cast<net::port_number>(1000 + i),
        });
        TEST_ASSERT_TRUE(svc.has_value());
        services.push_back(std::move(*svc));
    }
    auto extra = mdns::service::make({.type = "_extra", .protocol = mdns::protocol::udp, .port = 999});
    TEST_ASSERT_FALSE(extra.has_value());
    TEST_ASSERT_TRUE(extra.error() == mdns::errc::limit_reached);

    // Freeing one makes room again.
    services.pop_back();
    TEST_ASSERT_TRUE(mdns::service::make({.type = "_extra", .protocol = mdns::protocol::udp, .port = 999}).has_value());
}

// =============================================================================
// Delegated hosts
// =============================================================================

TEST_CASE("mdns delegated host answers for its name and carries services", "[idfxx][mdns]") {
    mdns_host_env env;

    const net::ipv4_addr addr(192, 168, 7, 50);
    auto host = mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {addr}});
    TEST_ASSERT_TRUE(host.has_value());
    TEST_ASSERT_EQUAL_STRING("idfxx-peer", host->name().c_str());
    TEST_ASSERT_TRUE(mdns::hostname_exists("idfxx-peer"));

    auto svc = mdns::service::make(*host, {.instance_name = "Peer Web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 8080});
    TEST_ASSERT_TRUE(svc.has_value());
    TEST_ASSERT_TRUE(svc->instance_name() == "Peer Web");
    TEST_ASSERT_TRUE(svc->port() == 8080);
    TEST_ASSERT_TRUE(mdns::service_exists(*host, "_http", mdns::protocol::tcp));
    TEST_ASSERT_TRUE(mdns::service_exists(*host, "_http", mdns::protocol::tcp, "Peer Web"));
    TEST_ASSERT_FALSE(mdns::service_exists("_http", mdns::protocol::tcp)); // not the device's

    auto delegated = mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp);
    TEST_ASSERT_TRUE(delegated.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, delegated->size());
    const mdns::answer& a = delegated->front();
    TEST_ASSERT_TRUE(a.hostname == "idfxx-peer");
    TEST_ASSERT_TRUE(a.instance_name == "Peer Web");
    TEST_ASSERT_TRUE(a.port == 8080);
    TEST_ASSERT_EQUAL_UINT32(1, a.ipv4_addresses.size());
    TEST_ASSERT_TRUE(a.ipv4_addresses.front() == addr);

    auto self = mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp);
    TEST_ASSERT_TRUE(self.has_value() && self->empty());

    // Service mutation on a delegated host
    TEST_ASSERT_TRUE(svc->try_set_port(8081).has_value());
    TEST_ASSERT_TRUE(svc->port() == 8081);
    TEST_ASSERT_TRUE(svc->try_set_txt_item({.key = "path", .value = "/peer"}).has_value());
    auto changed = mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp).value().front();
    TEST_ASSERT_TRUE(changed.port == 8081);
    TEST_ASSERT_TRUE(find_txt(changed, "path")->value == "/peer");

    const net::ipv4_addr addrs[] = {net::ipv4_addr(192, 168, 7, 51)};
    TEST_ASSERT_TRUE(host->try_set_addresses(addrs, {}).has_value());
    auto no_addrs = host->try_set_addresses({}, {});
    TEST_ASSERT_TRUE(!no_addrs && no_addrs.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("mdns delegated host rejects duplicate and invalid names", "[idfxx][mdns]") {
    mdns_host_env env;

    std::optional<mdns::delegated_host> host;
    host.emplace(mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}}).value());

    auto dup = mdns::delegated_host::make({.name = "IDFXX-PEER", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 3)}});
    TEST_ASSERT_TRUE(!dup && dup.error() == idfxx::errc::invalid_state);
    auto own = mdns::delegated_host::make({.name = "idfxx-test", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 3)}});
    TEST_ASSERT_TRUE(!own && own.error() == idfxx::errc::invalid_state);
    auto no_addrs = mdns::delegated_host::make({.name = "idfxx-other"});
    TEST_ASSERT_TRUE(!no_addrs && no_addrs.error() == idfxx::errc::invalid_arg);
    auto empty = mdns::delegated_host::make({.name = "", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 3)}});
    TEST_ASSERT_TRUE(!empty && empty.error() == idfxx::errc::invalid_arg);

    // The name can be reused as soon as the host is destroyed: the removal
    // queued by the destructor runs before the new add.
    host.reset();
    host.emplace(mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 4)}}).value());
    TEST_ASSERT_TRUE(mdns::hostname_exists("idfxx-peer"));
}

#ifdef CONFIG_LWIP_IPV6
TEST_CASE("mdns delegated host accepts IPv6 addresses", "[idfxx][mdns]") {
    mdns_host_env env;
    const auto v6 = *net::ipv6_addr::parse("fd00::50");
    auto host = mdns::delegated_host::make({.name = "idfxx-peer6", .ipv6_addresses = {v6}});
    TEST_ASSERT_TRUE(host.has_value());
    auto svc = mdns::service::make(*host, {.type = "_ssh", .protocol = mdns::protocol::tcp, .port = 22});
    TEST_ASSERT_TRUE(svc.has_value());
    auto found = mdns::try_lookup_services(mdns::delegated, "_ssh", mdns::protocol::tcp).value();
    TEST_ASSERT_EQUAL_UINT32(1, found.size());
    TEST_ASSERT_EQUAL_UINT32(1, found.front().ipv6_addresses.size());
    TEST_ASSERT_TRUE(found.front().ipv6_addresses.front() == v6);
}
#endif

TEST_CASE("mdns subtypes of same-named services on different hosts stay apart", "[idfxx][mdns]") {
    mdns_host_env env;
    auto self_with = [](const char* subtype) {
        return mdns::try_lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp, {.subtype = subtype}).value().size();
    };
    auto peer_with = [](const char* subtype) {
        return mdns::try_lookup_services(mdns::delegated, "_http", mdns::protocol::tcp, {.subtype = subtype}).value().size();
    };

    auto own = mdns::service::make({.instance_name = "web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 80});
    TEST_ASSERT_TRUE(own.has_value());
    auto host = mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(192, 168, 7, 50)}});
    TEST_ASSERT_TRUE(host.has_value());
    // Created last, so the C library lists it first.
    auto peer = mdns::service::make(*host, {.instance_name = "web", .type = "_http", .protocol = mdns::protocol::tcp, .port = 8080});
    TEST_ASSERT_TRUE(peer.has_value());

    TEST_ASSERT_TRUE(own->try_add_subtype("_own").has_value());
    TEST_ASSERT_EQUAL_UINT32(1, self_with("_own"));
    TEST_ASSERT_EQUAL_UINT32(0, peer_with("_own"));

    TEST_ASSERT_TRUE(peer->try_add_subtype("_peer").has_value());
    TEST_ASSERT_EQUAL_UINT32(1, peer_with("_peer"));
    TEST_ASSERT_EQUAL_UINT32(0, self_with("_peer"));

    const std::vector<std::string> subtypes = {"_a", "_b"};
    TEST_ASSERT_TRUE(own->try_set_subtypes(subtypes).has_value());
    TEST_ASSERT_EQUAL_UINT32(1, self_with("_a"));
    TEST_ASSERT_EQUAL_UINT32(0, self_with("_own"));
    TEST_ASSERT_EQUAL_UINT32(1, peer_with("_peer"));
    TEST_ASSERT_EQUAL_UINT32(0, peer_with("_a"));

    // The peer has no "_a" to remove, whatever the device's service has.
    auto missing = peer->try_remove_subtype("_a");
    TEST_ASSERT_TRUE(!missing && missing.error() == idfxx::errc::not_found);
    TEST_ASSERT_EQUAL_UINT32(1, self_with("_a"));

    TEST_ASSERT_TRUE(own->try_remove_subtype("_a").has_value());
    TEST_ASSERT_EQUAL_UINT32(0, self_with("_a"));
    TEST_ASSERT_EQUAL_UINT32(1, self_with("_b"));
    TEST_ASSERT_EQUAL_UINT32(1, peer_with("_peer"));
}

TEST_CASE("mdns hostname can't be a delegated host's name", "[idfxx][mdns]") {
    mdns_host_env env;
    std::optional<mdns::delegated_host> host;
    host.emplace(mdns::delegated_host::make({.name = "idfxx-peer", .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}}).value());

    auto r = mdns::try_set_hostname("IDFXX-PEER");
    TEST_ASSERT_TRUE(!r && r.error() == idfxx::errc::invalid_state);
    TEST_ASSERT_EQUAL_STRING("idfxx-test", mdns::try_get_hostname().value().c_str());

    // Free again once the host is gone.
    host.reset();
    TEST_ASSERT_TRUE(mdns::try_set_hostname("idfxx-peer").has_value());
}

TEST_CASE("mdns hostname and delegated host changes don't overtake each other", "[idfxx][mdns]") {
    mdns_host_env env;

    // Both calls wait for the mDNS task on one shared signal. Each must still
    // return only once its own change is applied.
    std::atomic<bool> stop{false};
    std::thread renamer([&] {
        for (int i = 0; !stop.load(); ++i) {
            std::ignore = mdns::try_set_hostname(i % 2 ? "idfxx-test" : "idfxx-test2");
            delay(idfxx::next_tick); // let the lower-priority test task take the lock too
        }
    });
    bool all_visible = true;
    for (int i = 0; i < 20; ++i) {
        const std::string name = "idfxx-peer" + std::to_string(i);
        auto host = mdns::delegated_host::make({.name = name, .ipv4_addresses = {net::ipv4_addr(10, 0, 0, 2)}});
        TEST_ASSERT_TRUE(host.has_value());
        all_visible = all_visible && mdns::hostname_exists(name);
    }
    stop.store(true);
    renamer.join();
    TEST_ASSERT_TRUE(all_visible);
}

// =============================================================================
// Network interfaces
// =============================================================================

// A network interface that no driver backs.
static netif::interface make_test_netif(const char* key, const char* desc) {
    esp_netif_inherent_config_t base = *ESP_NETIF_BASE_DEFAULT_ETH;
    base.if_key = key;
    base.if_desc = desc;
    base.route_prio = 1;
    esp_netif_config_t cfg = {.base = &base, .driver = nullptr, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    esp_netif_t* handle = esp_netif_new(&cfg);
    TEST_ASSERT_NOT_NULL(handle);
    return netif::interface::take(handle);
}

TEST_CASE("mdns register_netif fills the free interface slots", "[idfxx][mdns]") {
    mdns_env env;

    auto iface = make_test_netif("MDNS_TEST", "mdns test");

    auto r = mdns::try_register_netif(iface);
    if (predefined_netifs < CONFIG_MDNS_MAX_INTERFACES) {
        TEST_ASSERT_TRUE(r.has_value());
        auto again = mdns::try_register_netif(iface);
        TEST_ASSERT_TRUE(!again && again.error() == idfxx::errc::invalid_state);
        TEST_ASSERT_TRUE(mdns::try_post_netif_action(iface, mdns::netif_action::enable_ipv4 | mdns::netif_action::announce_ipv4).has_value());
        TEST_ASSERT_TRUE(mdns::try_unregister_netif(iface).has_value());
        // Unregistering waits for the mDNS task, so the interface is gone at once.
        auto after = mdns::try_post_netif_action(iface, mdns::netif_action::announce_ipv4);
        TEST_ASSERT_TRUE(!after && after.error() == idfxx::errc::invalid_state);
        // The freed slot takes the interface again.
        TEST_ASSERT_TRUE(mdns::try_register_netif(iface).has_value());
        TEST_ASSERT_TRUE(mdns::try_post_netif_action(iface, mdns::netif_action::enable_ipv4).has_value());
        TEST_ASSERT_TRUE(mdns::try_unregister_netif(iface).has_value());

        // A disable action that the caller posts unregisters the interface too.
        TEST_ASSERT_TRUE(mdns::try_register_netif(iface).has_value());
        TEST_ASSERT_TRUE(mdns::try_post_netif_action(iface, mdns::netif_action::disable_ipv4).has_value());
        for (int i = 0; i < 100 && mdns::try_post_netif_action(iface, {}).has_value(); ++i) {
            delay(10ms);
        }
        auto disabled = mdns::try_unregister_netif(iface);
        TEST_ASSERT_TRUE(!disabled && disabled.error() == idfxx::errc::not_found);
        TEST_ASSERT_TRUE(mdns::try_register_netif(iface).has_value());
        TEST_ASSERT_TRUE(mdns::try_unregister_netif(iface).has_value());
    } else {
        // The predefined interfaces take every slot.
        TEST_ASSERT_FALSE(r.has_value());
        TEST_ASSERT_TRUE(r.error() == mdns::errc::limit_reached);
    }
    auto gone = mdns::try_unregister_netif(iface);
    TEST_ASSERT_TRUE(!gone && gone.error() == idfxx::errc::not_found);
    auto action = mdns::try_post_netif_action(iface, mdns::netif_action::enable_ipv4);
    TEST_ASSERT_TRUE(!action && action.error() == idfxx::errc::invalid_state);
}

#ifdef CONFIG_MDNS_PREDEF_NETIF_STA
TEST_CASE("mdns unregister_netif leaves a default interface alone", "[idfxx][mdns]") {
    // Made before mDNS starts, and destroyed after it stops: mDNS keeps a
    // pointer to a default interface once it has found it by its key.
    std::optional<netif::interface> sta = netif::find_by_key("WIFI_STA_DEF");
    if (!sta) {
        sta.emplace(make_test_netif("WIFI_STA_DEF", "mdns test sta"));
    }
    mdns_env env;

    auto r = mdns::try_unregister_netif(*sta);
    TEST_ASSERT_TRUE(!r && r.error() == idfxx::errc::not_found);
    TEST_ASSERT_TRUE(mdns::try_post_netif_action(*sta, mdns::netif_action::announce_ipv4).has_value());
}
#endif

// =============================================================================
// Browsing
// =============================================================================

#ifdef CONFIG_MDNS_ENABLE_BROWSE
TEST_CASE("mdns browser starts and stops", "[idfxx][mdns]") {
    mdns_env env;

    std::atomic<int> calls{0};
    auto cb = [&](const mdns::answer&) { calls++; };
    auto b = mdns::browser::make({.type = "_http", .protocol = mdns::protocol::tcp}, cb);
    TEST_ASSERT_TRUE(b.has_value());

    // One browser per type, protocol and subtype (compared without case).
    auto dup = mdns::browser::make({.type = "_HTTP", .protocol = mdns::protocol::tcp}, cb);
    TEST_ASSERT_TRUE(!dup && dup.error() == idfxx::errc::invalid_state);
    auto udp = mdns::browser::make({.type = "_http", .protocol = mdns::protocol::udp}, cb);
    TEST_ASSERT_TRUE(udp.has_value());
    auto sub = mdns::browser::make({.type = "_http", .protocol = mdns::protocol::tcp, .subtype = "_printer"}, cb);
    TEST_ASSERT_TRUE(sub.has_value());

    auto no_cb = mdns::browser::make({.type = "_ftp", .protocol = mdns::protocol::tcp}, nullptr);
    TEST_ASSERT_TRUE(!no_cb && no_cb.error() == idfxx::errc::invalid_arg);
    auto long_sub = mdns::browser::make({.type = "_ftp", .protocol = mdns::protocol::tcp, .subtype = std::string(64, 's')}, cb);
    TEST_ASSERT_TRUE(!long_sub && long_sub.error() == idfxx::errc::invalid_arg);
    auto long_type = mdns::browser::make({.type = std::string(64, 't'), .protocol = mdns::protocol::tcp}, cb);
    TEST_ASSERT_TRUE(!long_type && long_type.error() == idfxx::errc::invalid_arg);

    delay(150ms);
    // Destroying a browser frees its key at once.
    b = mdns::browser::make({.type = "_ssh", .protocol = mdns::protocol::tcp}, cb);
    TEST_ASSERT_TRUE(b.has_value());
    auto again = mdns::browser::make({.type = "_http", .protocol = mdns::protocol::tcp}, cb);
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_EQUAL_INT(0, calls.load()); // nothing on the network to report
}
#endif

// =============================================================================
// Queries
// =============================================================================

TEST_CASE("mdns start_query completes empty at its timeout", "[idfxx][mdns]") {
    mdns_env env;

    auto f = mdns::try_start_query({
        .service_type = "_http",
        .protocol = mdns::protocol::tcp,
        .record_type = mdns::record_type::ptr,
        .timeout = 200ms,
    });
    TEST_ASSERT_TRUE(f.has_value());
    TEST_ASSERT_TRUE(f->valid());
    TEST_ASSERT_FALSE(f->done());

    auto early = f->try_wait_for(20ms);
    TEST_ASSERT_TRUE(!early && early.error() == idfxx::errc::timeout);

    auto copy = *f;
    auto answers = f->try_wait_for(1s);
    TEST_ASSERT_TRUE(answers.has_value());
    TEST_ASSERT_TRUE(answers->empty());
    TEST_ASSERT_TRUE(f->done());
    TEST_ASSERT_TRUE(copy.done());

    // The answers are cached for every copy and later wait.
    auto again = copy.try_wait();
    TEST_ASSERT_TRUE(again.has_value() && again->empty());
    TEST_ASSERT_TRUE(f->try_wait_for(0ms).has_value());
}

TEST_CASE("mdns start_query validates its configuration", "[idfxx][mdns]") {
    mdns_env env;

    auto zero = mdns::try_start_query({.name = "host", .record_type = mdns::record_type::a, .timeout = 0ms});
    TEST_ASSERT_TRUE(!zero && zero.error() == idfxx::errc::invalid_arg);
    auto half = mdns::try_start_query({.service_type = "_http", .record_type = mdns::record_type::ptr});
    TEST_ASSERT_TRUE(!half && half.error() == idfxx::errc::invalid_arg);
    auto max0 = mdns::try_start_query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .max_results = 0});
    TEST_ASSERT_TRUE(!max0 && max0.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("mdns queries need the fields of their record type", "[idfxx][mdns]") {
    mdns_env env;
    auto rejected = [](const mdns::query_config& cfg) {
        auto q = mdns::try_query(cfg);
        auto sq = mdns::try_start_query(cfg);
        return !q && q.error() == idfxx::errc::invalid_arg && !sq && sq.error() == idfxx::errc::invalid_arg;
    };
    using mdns::record_type;
    constexpr auto tcp = mdns::protocol::tcp;

    // PTR needs the service type.
    TEST_ASSERT_TRUE(rejected({.record_type = record_type::ptr}));
    TEST_ASSERT_TRUE(rejected({.name = "Nobody", .record_type = record_type::ptr}));
    // SRV and TXT need the instance name and the service type.
    TEST_ASSERT_TRUE(rejected({.service_type = "_http", .protocol = tcp, .record_type = record_type::srv}));
    TEST_ASSERT_TRUE(rejected({.name = "Nobody", .record_type = record_type::srv}));
    TEST_ASSERT_TRUE(rejected({.service_type = "_http", .protocol = tcp, .record_type = record_type::txt}));
    TEST_ASSERT_TRUE(rejected({.name = "Nobody", .record_type = record_type::txt}));
    // A and AAAA need the hostname.
    TEST_ASSERT_TRUE(rejected({.record_type = record_type::a}));
    TEST_ASSERT_TRUE(rejected({.record_type = record_type::aaaa}));
    // A name with a NUL in it.
    TEST_ASSERT_TRUE(rejected({.name = std::string("idfxx\0nobody", 12), .record_type = record_type::a}));

    // The complete configurations run, and find nothing.
    auto srv = mdns::try_query({.name = "Nobody", .service_type = "_http", .protocol = tcp, .record_type = record_type::srv, .timeout = 100ms});
    TEST_ASSERT_TRUE(srv.has_value() && srv->empty());
    auto a = mdns::try_query({.name = "idfxx-nobody", .record_type = record_type::a, .timeout = 100ms});
    TEST_ASSERT_TRUE(a.has_value() && a->empty());
}

TEST_CASE("mdns queries reject a NaN timeout", "[idfxx][mdns]") {
    mdns_env env;
    const std::chrono::duration<double, std::milli> nan_ms(std::numeric_limits<double>::quiet_NaN());
    const std::chrono::duration<float> nan_s(std::numeric_limits<float>::quiet_NaN());

    auto ptr = mdns::try_query_ptr("_http", mdns::protocol::tcp, nan_ms);
    TEST_ASSERT_TRUE(!ptr && ptr.error() == idfxx::errc::invalid_arg);
    auto srv = mdns::try_query_srv("Nobody", "_http", mdns::protocol::tcp, nan_s);
    TEST_ASSERT_TRUE(!srv && srv.error() == idfxx::errc::invalid_arg);
    auto txt = mdns::try_query_txt("Nobody", "_http", mdns::protocol::tcp, nan_ms);
    TEST_ASSERT_TRUE(!txt && txt.error() == idfxx::errc::invalid_arg);
#ifdef CONFIG_LWIP_IPV4
    auto a = mdns::try_query_a("idfxx-nobody", nan_s);
    TEST_ASSERT_TRUE(!a && a.error() == idfxx::errc::invalid_arg);
#endif
#ifdef CONFIG_LWIP_IPV6
    auto aaaa = mdns::try_query_aaaa("idfxx-nobody", nan_ms);
    TEST_ASSERT_TRUE(!aaaa && aaaa.error() == idfxx::errc::invalid_arg);
#endif
    // A positive floating-point timeout still rounds up to whole milliseconds.
    auto tiny = mdns::try_query_ptr("_http", mdns::protocol::tcp, std::chrono::duration<double, std::milli>(0.25));
    TEST_ASSERT_TRUE(tiny.has_value() && tiny->empty());
}

TEST_CASE("mdns query future keeps its answers after the query finishes", "[idfxx][mdns]") {
    bool created_loop = event_loop::try_create_system().has_value();
    TEST_ASSERT_TRUE(mdns::try_init().has_value());
    const mdns::query_config cfg{.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 100ms};
    auto wait_done = [](const future<std::vector<mdns::answer>>& f) {
        for (int i = 0; i < 100 && !f.done(); ++i) {
            delay(10ms);
        }
        return f.done();
    };

    // Collected in the background once finished, before anyone waits.
    auto f = mdns::try_start_query(cfg).value();
    TEST_ASSERT_TRUE(wait_done(f));
    delay(50ms);
    auto answers = f.try_wait_for(0ms);
    TEST_ASSERT_TRUE(answers.has_value() && answers->empty());
    auto again = f.try_wait();
    TEST_ASSERT_TRUE(again.has_value() && again->empty());

    // A wait as soon as the query finishes.
    auto g = mdns::try_start_query(cfg).value();
    TEST_ASSERT_TRUE(wait_done(g));
    answers = g.try_wait_for(0ms);
    TEST_ASSERT_TRUE(answers.has_value() && answers->empty());

    // deinit() with finished futures that nobody has waited on, one of them
    // just finished.
    auto h = mdns::try_start_query(cfg).value();
    auto k = mdns::try_start_query(cfg).value();
    TEST_ASSERT_TRUE(wait_done(h));
    delay(50ms);
    TEST_ASSERT_TRUE(wait_done(k));
    mdns::deinit();
    answers = h.try_wait_for(0ms);
    TEST_ASSERT_TRUE(answers.has_value() && answers->empty());
    answers = k.try_wait_for(0ms);
    TEST_ASSERT_TRUE(answers.has_value() && answers->empty());

    if (created_loop) {
        std::ignore = event_loop::try_destroy_system();
    }
}

TEST_CASE("mdns queries reject timeouts the mDNS library can't represent", "[idfxx][mdns]") {
    mdns_env env;

    const auto too_long = std::chrono::milliseconds(int64_t{INT32_MAX} + 1);
    const mdns::query_config cfg{.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = too_long};
    auto q = mdns::try_query(cfg);
    TEST_ASSERT_TRUE(!q && q.error() == idfxx::errc::invalid_arg);
    auto qt = mdns::try_query(cfg, mdns::transmission::unicast);
    TEST_ASSERT_TRUE(!qt && qt.error() == idfxx::errc::invalid_arg);
    auto sq = mdns::try_start_query(cfg);
    TEST_ASSERT_TRUE(!sq && sq.error() == idfxx::errc::invalid_arg);

    auto ptr = mdns::try_query_ptr("_http", mdns::protocol::tcp, too_long);
    TEST_ASSERT_TRUE(!ptr && ptr.error() == idfxx::errc::invalid_arg);
    // Durations whose conversion to milliseconds would overflow.
    auto ptr_huge = mdns::try_query_ptr("_http", mdns::protocol::tcp, std::chrono::hours::max());
    TEST_ASSERT_TRUE(!ptr_huge && ptr_huge.error() == idfxx::errc::invalid_arg);
    auto ptr_min = mdns::try_query_ptr("_http", mdns::protocol::tcp, std::chrono::seconds::min());
    TEST_ASSERT_TRUE(!ptr_min && ptr_min.error() == idfxx::errc::invalid_arg);
    auto srv = mdns::try_query_srv("Nobody", "_http", mdns::protocol::tcp, too_long);
    TEST_ASSERT_TRUE(!srv && srv.error() == idfxx::errc::invalid_arg);
    auto txt = mdns::try_query_txt("Nobody", "_http", mdns::protocol::tcp, std::chrono::hours::max());
    TEST_ASSERT_TRUE(!txt && txt.error() == idfxx::errc::invalid_arg);
#ifdef CONFIG_LWIP_IPV4
    auto a = mdns::try_query_a("idfxx-nobody", too_long);
    TEST_ASSERT_TRUE(!a && a.error() == idfxx::errc::invalid_arg);
#endif
#ifdef CONFIG_LWIP_IPV6
    auto aaaa = mdns::try_query_aaaa("idfxx-nobody", too_long);
    TEST_ASSERT_TRUE(!aaaa && aaaa.error() == idfxx::errc::invalid_arg);
#endif
}

TEST_CASE("mdns queries reject names the mDNS library can't store", "[idfxx][mdns]") {
    mdns_env env;
    const std::string long_name(64, 'n');

    auto name = mdns::try_query({.name = long_name, .record_type = mdns::record_type::a, .timeout = 100ms});
    TEST_ASSERT_TRUE(!name && name.error() == idfxx::errc::invalid_arg);
    auto type = mdns::try_start_query({.service_type = long_name, .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 100ms});
    TEST_ASSERT_TRUE(!type && type.error() == idfxx::errc::invalid_arg);
    auto ptr = mdns::try_query_ptr(long_name, mdns::protocol::tcp, 100ms);
    TEST_ASSERT_TRUE(!ptr && ptr.error() == idfxx::errc::invalid_arg);
    auto srv = mdns::try_query_srv(long_name, "_http", mdns::protocol::tcp, 100ms);
    TEST_ASSERT_TRUE(!srv && srv.error() == idfxx::errc::invalid_arg);
#ifdef CONFIG_LWIP_IPV4
    auto a = mdns::try_query_a(long_name, 100ms);
    TEST_ASSERT_TRUE(!a && a.error() == idfxx::errc::invalid_arg);
#endif
}

TEST_CASE("mdns query future waits without limit for an unbounded timeout", "[idfxx][mdns]") {
    mdns_env env;
    auto f = mdns::try_start_query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 100ms});
    TEST_ASSERT_TRUE(f.has_value());
    auto answers = f->try_wait_for(std::chrono::milliseconds::max());
    TEST_ASSERT_TRUE(answers.has_value());
    TEST_ASSERT_TRUE(answers->empty());
}

TEST_CASE("mdns query future waits with timeouts too long for one condition wait", "[idfxx][mdns]") {
    mdns_env env;
    const mdns::query_config cfg{.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 100ms};

    auto f = mdns::try_start_query(cfg);
    TEST_ASSERT_TRUE(f.has_value());
    auto answers = f->try_wait_for(std::chrono::milliseconds(0xFFFFFFFF));
    TEST_ASSERT_TRUE(answers.has_value());
    TEST_ASSERT_TRUE(answers->empty());

    auto g = mdns::try_start_query(cfg);
    TEST_ASSERT_TRUE(g.has_value());
    answers = g->try_wait_for(std::chrono::days(30));
    TEST_ASSERT_TRUE(answers.has_value());
    TEST_ASSERT_TRUE(answers->empty());
}

TEST_CASE("mdns dropping query futures neither blocks nor leaks", "[idfxx][mdns]") {
    mdns_env env;

    const mdns::query_config cfg{
        .service_type = "_http",
        .protocol = mdns::protocol::tcp,
        .record_type = mdns::record_type::ptr,
        .timeout = 200ms,
    };

    // Warm up allocations made once (registry storage, timer).
    for (int i = 0; i < 2; ++i) {
        std::ignore = mdns::try_start_query(cfg).value();
        auto done = mdns::try_start_query(cfg).value();
        std::ignore = done.try_wait();
    }
    delay(600ms);
    const size_t baseline = free_heap();

    constexpr int rounds = 10;
    for (int i = 0; i < rounds; ++i) {
        auto f = mdns::try_start_query(cfg);
        TEST_ASSERT_TRUE(f.has_value());
        auto copy = *f;
        auto start = std::chrono::steady_clock::now();
        f = idfxx::error(idfxx::errc::fail); // drop one copy
        copy = {};                           // drop the last one, before the query has finished
        auto elapsed = std::chrono::steady_clock::now() - start;
        TEST_ASSERT_TRUE(elapsed < 20ms);
    }

    // A future dropped after its query finished (and never waited on).
    {
        auto f = mdns::try_start_query({.service_type = "_ftp", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 50ms}).value();
        for (int i = 0; i < 100 && !f.done(); ++i) {
            delay(10ms);
        }
        TEST_ASSERT_TRUE(f.done());
    }

    // Every abandoned query ends at its timeout and is freed in the background.
    delay(800ms);
    const size_t after = free_heap();
    TEST_ASSERT_TRUE_MESSAGE(after + 512 >= baseline, "abandoned queries were not freed");
}

TEST_CASE("mdns deinit lets queries in progress complete", "[idfxx][mdns]") {
    bool created_loop = event_loop::try_create_system().has_value();
    TEST_ASSERT_TRUE(mdns::try_init().has_value());

    const mdns::query_config cfg{
        .service_type = "_http",
        .protocol = mdns::protocol::tcp,
        .record_type = mdns::record_type::ptr,
        .timeout = 300ms,
    };
    auto live = mdns::try_start_query(cfg).value();
    std::ignore = mdns::try_start_query(cfg).value(); // abandoned while running

    mdns::deinit();
    TEST_ASSERT_TRUE(live.done());
    auto answers = live.try_wait_for(0ms);
    TEST_ASSERT_TRUE(answers.has_value() && answers->empty());

    if (created_loop) {
        std::ignore = event_loop::try_destroy_system();
    }
}

TEST_CASE("mdns deinit waits for a query being collected on another task", "[idfxx][mdns]") {
    bool created_loop = event_loop::try_create_system().has_value();
    TEST_ASSERT_TRUE(mdns::try_init().has_value());

    auto f = mdns::try_start_query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 200ms}).value();
    std::optional<result<std::vector<mdns::answer>>> seen;
    std::thread waiter([&] { seen = f.try_wait(); });
    mdns::deinit(); // races the waiter's collection of the finished query
    waiter.join();
    TEST_ASSERT_TRUE(seen.has_value());
    TEST_ASSERT_TRUE(seen->has_value() && (*seen)->empty());

    if (created_loop) {
        std::ignore = event_loop::try_destroy_system();
    }
}

TEST_CASE("mdns blocking queries find nothing without a network", "[idfxx][mdns]") {
    mdns_env env;

    auto q = mdns::try_query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 100ms});
    TEST_ASSERT_TRUE(q.has_value() && q->empty());

    auto ptr = mdns::try_query_ptr("_http", mdns::protocol::tcp, 100ms);
    TEST_ASSERT_TRUE(ptr.has_value() && ptr->empty());
    auto srv = mdns::try_query_srv("Nobody", "_http", mdns::protocol::tcp, 100ms);
    TEST_ASSERT_TRUE(srv.has_value() && !srv->has_value());
    auto txt = mdns::try_query_txt("Nobody", "_http", mdns::protocol::tcp, 100ms);
    TEST_ASSERT_TRUE(txt.has_value() && !txt->has_value());

    auto bad = mdns::try_query_ptr("", mdns::protocol::tcp, 100ms);
    TEST_ASSERT_TRUE(!bad && bad.error() == idfxx::errc::invalid_arg);
    auto neg = mdns::try_query_ptr("_http", mdns::protocol::tcp, -1ms);
    TEST_ASSERT_TRUE(!neg && neg.error() == idfxx::errc::invalid_arg);
}

TEST_CASE("mdns query with a chosen transmission finds nothing without a network", "[idfxx][mdns]") {
    mdns_env env;

    // PTR queries default to multicast. Ask for unicast answers instead.
    auto unicast = mdns::try_query(
        {.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 100ms},
        mdns::transmission::unicast
    );
    TEST_ASSERT_TRUE(unicast.has_value() && unicast->empty());

    // Other queries default to unicast. Ask for multicast answers instead.
    auto multicast = mdns::try_query(
        {.name = "idfxx-nobody", .record_type = mdns::record_type::a, .timeout = 100ms},
        mdns::transmission::multicast
    );
    TEST_ASSERT_TRUE(multicast.has_value() && multicast->empty());

    // The configuration is validated as for the default transmission.
    auto zero = mdns::try_query({.name = "host", .record_type = mdns::record_type::a, .timeout = 0ms}, mdns::transmission::multicast);
    TEST_ASSERT_TRUE(!zero && zero.error() == idfxx::errc::invalid_arg);
    auto half = mdns::try_query({.service_type = "_http", .record_type = mdns::record_type::ptr}, mdns::transmission::unicast);
    TEST_ASSERT_TRUE(!half && half.error() == idfxx::errc::invalid_arg);
}

#ifdef CONFIG_LWIP_IPV4
TEST_CASE("mdns query_a returns nullopt when nothing answers", "[idfxx][mdns]") {
    mdns_env env;
    auto a = mdns::try_query_a("idfxx-nobody", 100ms);
    TEST_ASSERT_TRUE(a.has_value());
    TEST_ASSERT_FALSE(a->has_value());
    auto empty = mdns::try_query_a("", 100ms);
    TEST_ASSERT_TRUE(!empty && empty.error() == idfxx::errc::invalid_arg);
}
#endif

#ifdef CONFIG_LWIP_IPV6
TEST_CASE("mdns query_aaaa returns nullopt when nothing answers", "[idfxx][mdns]") {
    mdns_env env;
    auto a = mdns::try_query_aaaa("idfxx-nobody", 100ms);
    TEST_ASSERT_TRUE(a.has_value());
    TEST_ASSERT_FALSE(a->has_value());
}
#endif

// =============================================================================
// Exception-based API
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
TEST_CASE("mdns throwing API raises system_error", "[idfxx][mdns]") {
    mdns::deinit();
    bool threw = false;
    try {
        mdns::set_hostname("idfxx-test");
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::invalid_state;
    }
    TEST_ASSERT_TRUE(threw);

    mdns_env env;
    mdns::set_hostname("idfxx-test");
    TEST_ASSERT_EQUAL_STRING("idfxx-test", mdns::get_hostname().c_str());
    mdns::service web({.type = "_http", .protocol = mdns::protocol::tcp, .port = 80, .txt = {{.key = "path", .value = "/"}}});
    web.set_port(8080);
    TEST_ASSERT_TRUE(mdns::lookup_services(mdns::self_hosted, "_http", mdns::protocol::tcp).front().port == 8080);

    threw = false;
    try {
        mdns::service dup({.type = "_http", .protocol = mdns::protocol::tcp, .port = 81});
    } catch (const std::system_error& e) {
        threw = e.code() == idfxx::errc::invalid_state;
    }
    TEST_ASSERT_TRUE(threw);

    TEST_ASSERT_TRUE(mdns::query_ptr("_http", mdns::protocol::tcp, 100ms).empty());
    TEST_ASSERT_TRUE(mdns::query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 100ms}, mdns::transmission::unicast).empty());
    TEST_ASSERT_TRUE(mdns::start_query({.service_type = "_http", .protocol = mdns::protocol::tcp, .record_type = mdns::record_type::ptr, .timeout = 50ms}).wait().empty());
}
#endif
