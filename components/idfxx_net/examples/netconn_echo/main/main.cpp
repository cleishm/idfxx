// SPDX-License-Identifier: Apache-2.0

// Netconn echo server demonstrating zero-copy segment walking.
// Most callers should prefer the BSD socket API -- see the `tcp_echo_server`
// example for the equivalent built on `idfxx::net::listener` and `socket`.

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/log>
#include <idfxx/net/netconn/listener>
#include <idfxx/net/netconn/netbuf>
#include <idfxx/net/netconn/stream_connection>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;
namespace nc = idfxx::net::netconn;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static constexpr idfxx::net::port_number ECHO_PORT = 8081;

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

        // --- Netconn TCP echo server ---
        nc::listener srv;
        srv.bind({idfxx::net::ipv4_addr::any(), ECHO_PORT});
        srv.listen();
        logger.info("Netconn echo on {}:{} -- try: nc {} {}", my_ip, ECHO_PORT, my_ip, ECHO_PORT);

        while (true) {
            nc::stream_connection client = srv.accept();
            logger.info("Accepted connection");

            // recv() returns a non-owning netbuf (`!is_open()`) on graceful peer close.
            // Walk segments without copying via data()/advance(), echoing each one back.
            while (true) {
                nc::netbuf buf = client.recv();
                if (!buf.is_open()) {
                    break;
                }
                do {
                    auto seg = buf.data();
                    logger.info("segment: {} bytes", seg.size());
                    client.write(seg);
                } while (buf.advance());
            }
            logger.info("Client closed");
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
