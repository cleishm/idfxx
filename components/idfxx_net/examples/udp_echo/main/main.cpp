// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/log>
#include <idfxx/net/datagram_socket>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static constexpr idfxx::net::port_number ECHO_PORT = 5005;

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
        auto my_ip = connect_to_wifi();

        // --- UDP echo server ---
        idfxx::net::datagram_socket sock(idfxx::net::family::ipv4);
        sock.bind({idfxx::net::ipv4_addr::any(), ECHO_PORT});
        logger.info("UDP echo on {}:{} -- try: echo hi | nc -u {} {}", my_ip, ECHO_PORT, my_ip, ECHO_PORT);

        std::array<std::byte, 1500> buf; // sized for a typical Ethernet MTU
        while (true) {
            auto [data, from] = sock.recv_from(buf);
            logger.info("Got {} bytes from {}", data.size(), from);
            sock.send_to(data, from);
        }

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
