// SPDX-License-Identifier: Apache-2.0
//
// Demonstrates the DNS resolver: `resolve` for the full result set,
// `resolve_one` for the first match, family filtering, and the
// `numeric_host` option (parses a literal address with no DNS lookup).

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/log>
#include <idfxx/net/resolver>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static constexpr std::array<std::string_view, 3> HOSTS = {
    "example.com",
    "google.com",
    "anthropic.com",
};

enum class sync_flag : uint32_t {
    got_ip = 1u << 0,
};

template<>
inline constexpr bool idfxx::enable_flags_operators<sync_flag> = true;

namespace {
void resolve_host(std::string_view host, idfxx::net::port_number port, const idfxx::net::resolver_options& opts = {});
idfxx::net::ipv4_addr connect_to_wifi();
} // namespace

extern "C" void app_main() {
    try {
        connect_to_wifi();

        // --- Default resolution (all families, all matches) ---
        for (auto host : HOSTS) {
            resolve_host(host, 443);
        }

        // --- IPv4 only ---
        logger.info("--- IPv4 only ---");
        resolve_host("example.com", 80, {.family = idfxx::net::family::ipv4});

        // --- Parse a literal address with no DNS query ---
        logger.info("--- numeric_host (no DNS query) ---");
        auto literal = idfxx::net::resolve_one("8.8.8.8", 53, {.numeric_host = true});
        logger.info("Parsed literal: {}", literal);

        // --- First-match shortcut ---
        logger.info("--- resolve_one ---");
        auto first = idfxx::net::resolve_one("example.com", 80);
        logger.info("First match for example.com:80 is {}", first);

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}

namespace {

void resolve_host(std::string_view host, idfxx::net::port_number port, const idfxx::net::resolver_options& opts) {
    logger.info("Resolving {}:{}...", host, port);
    try {
        auto eps = idfxx::net::resolve(host, port, opts);
        for (const auto& ep : eps) {
            logger.info("  -> {}", ep);
        }
    } catch (const std::system_error& e) {
        logger.warn("  failed: {}", e.what());
    }
}

// Brings up the WiFi STA, blocks until DHCP assigns an IPv4 address, and
// registers an auto-reconnect listener for the rest of the program's lifetime.
// Returns the assigned address. Call once.
idfxx::net::ipv4_addr connect_to_wifi() {
    idfxx::event_loop::create_system();
    idfxx::netif::init();
    static auto sta_netif = idfxx::wifi::create_default_sta_netif();
    static idfxx::event_group<sync_flag> network_ready;
    static idfxx::net::ipv4_addr my_ip{};

    auto& loop = idfxx::event_loop::system();

    loop.listener_add(idfxx::netif::sta_got_ip4, [](const idfxx::netif::ip4_event_data& info) {
        my_ip = info.ip4.ip;
        logger.info("Got IP: {}", info.ip4.ip);
        network_ready.set(sync_flag::got_ip);
    });

    loop.listener_add(idfxx::wifi::sta_disconnected, [](const idfxx::wifi::disconnected_event_data& info) {
        logger.warn("Disconnected (reason: {}), reconnecting...", info.reason);
        // Listener runs in the event-loop trampoline (C frame); exceptions
        // must not escape, so suppress at this boundary.
        try {
            idfxx::wifi::connect();
        } catch (const std::system_error& e) {
            logger.warn("reconnect failed: {}", e.what());
        }
    });

    idfxx::wifi::init();
    idfxx::wifi::set_roles(idfxx::wifi::role::sta);
    idfxx::wifi::set_sta_config({
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .auth_threshold = idfxx::wifi::auth_mode::wpa2_psk,
    });
    idfxx::wifi::start();
    idfxx::wifi::connect();

    logger.info("Connecting to {}...", WIFI_SSID);
    network_ready.wait(sync_flag::got_ip);

    return my_ip;
}

} // namespace
