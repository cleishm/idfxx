// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto ap_netif = idfxx::wifi::create_default_ap_netif();

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::wifi::ap_sta_connected, [](const idfxx::wifi::ap_sta_connected_event_data& info) {
            logger.info("Station connected (AID: {})", info.aid);
        });

        loop.listener_add(
            idfxx::wifi::ap_sta_disconnected,
            [](const idfxx::wifi::ap_sta_disconnected_event_data& info) {
                logger.info("Station disconnected (AID: {}, reason: {})", info.aid, info.reason);
            }
        );

        // --- Initialize and start AP ---
        idfxx::wifi::init();
        idfxx::wifi::set_roles(idfxx::wifi::role::ap);
        idfxx::wifi::set_ap_config({
            .ssid = "idfxx_ap",
            .password = "password123",
            .channel = 6,
            .authmode = idfxx::wifi::auth_mode::wpa2_psk,
            .max_connection = 4,
        });
        idfxx::wifi::start();

        logger.info("Access point started: SSID=idfxx_ap, channel=6");

        // --- Periodically report connected stations ---
        while (true) {
            auto stations = idfxx::wifi::get_sta_list();
            if (!stations.empty()) {
                logger.info("{} station(s) connected:", stations.size());
                for (const auto& sta : stations) {
                    logger.info("  RSSI: {}", sta.rssi);
                }
            }
            idfxx::delay(10s);
        }

    } catch (const std::system_error& e) {
        logger.error("WiFi error: {}", e.what());
    }
}
