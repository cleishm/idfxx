// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto sta_netif = idfxx::wifi::create_default_sta_netif();

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::netif::sta_got_ip4, [&sta_netif](const idfxx::netif::ip4_event_data& info) {
            logger.info("Got IP: {}", info.ip4.ip);
            logger.info("Netmask: {}", info.ip4.netmask);
            logger.info("Gateway: {}", info.ip4.gateway);
            logger.info("Changed: {}", info.changed);

            // Query interface properties
            logger.info("Interface key: {}", sta_netif.key());
            logger.info("Interface description: {}", sta_netif.description());
            logger.info("Interface is up: {}", sta_netif.is_up());
            logger.info("Route priority: {}", sta_netif.get_route_priority());

            // Read back the IP info from the interface
            auto ip = sta_netif.get_ip4_info();
            logger.info("IP info from interface: {}", ip);

            // Query hostname
            auto hostname = sta_netif.get_hostname();
            logger.info("Hostname: {}", hostname);

            // Query MAC address
            auto mac = sta_netif.get_mac();
            logger.info("MAC: {}", mac);

            // Query DHCP client status
            logger.info("DHCP client running: {}", sta_netif.is_dhcp_client_running());

            // Query DNS servers
            for (auto type :
                 {idfxx::netif::dns_type::main, idfxx::netif::dns_type::backup, idfxx::netif::dns_type::fallback}) {
                auto dns = sta_netif.get_dns(type);
                if (!dns.ip.is_any()) {
                    logger.info("DNS {}: {}", type, dns.ip);
                }
            }
        });

        // --- Initialize and connect WiFi ---
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

        // --- Main loop ---
        while (true) {
            idfxx::delay(1h);
        }

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }
}
