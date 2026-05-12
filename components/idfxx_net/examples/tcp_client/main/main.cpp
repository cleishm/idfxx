// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/log>
#include <idfxx/net/resolver>
#include <idfxx/net/stream_socket>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static constexpr auto HOST = "example.com";
static constexpr idfxx::net::port_number PORT = 80;

enum class sync_flag : uint32_t {
    got_ip = 1u << 0,
};

template<>
inline constexpr bool idfxx::enable_flags_operators<sync_flag> = true;

namespace {
idfxx::net::ipv4_addr connect_to_wifi();
} // namespace

extern "C" void app_main() {
    try {
        connect_to_wifi();

        // --- DNS resolve + TCP client ---
        logger.info("Resolving {}...", HOST);
        auto eps = idfxx::net::resolve(HOST, PORT);
        logger.info("Resolved {} ({} endpoint{})", eps.front(), eps.size(), eps.size() == 1 ? "" : "s");

        idfxx::net::stream_socket sock(eps.front(), {.no_delay = true});
        sock.set_recv_timeout(5s);

        // `Connection: close` so the server hangs up after the response, giving us a clean EOF.
        constexpr std::string_view request = "GET / HTTP/1.0\r\n"
                                             "Host: example.com\r\n"
                                             "Connection: close\r\n"
                                             "\r\n";
        logger.info("Sending request ({} bytes)", request.size());
        sock.send_all(std::as_bytes(std::span(request)));

        std::array<std::byte, 1024> buf;
        size_t total = 0;
        while (true) {
            auto data = sock.recv(buf);
            if (data.empty()) {
                break;
            }
            total += data.size();
            std::string_view chunk(reinterpret_cast<const char*>(data.data()), data.size());
            logger.info("<- {}", chunk);
        }
        logger.info("Server closed; received {} bytes total", total);

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}

namespace {

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
