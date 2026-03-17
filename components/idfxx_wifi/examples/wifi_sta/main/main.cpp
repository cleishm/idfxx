// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>
#include <esp_netif.h>
#include <esp_wifi_default.h>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        esp_netif_init();
        esp_netif_create_default_wifi_sta();

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::wifi::sta_connected, [](const idfxx::wifi::connected_info& info) {
            logger.info("Connected to {} (channel {}, {})", info.ssid, info.channel, info.authmode);
        });

        loop.listener_add(idfxx::wifi::sta_disconnected, [](const idfxx::wifi::disconnected_info& info) {
            logger.warn("Disconnected from {} (reason: {})", info.ssid, info.reason);
            // Attempt to reconnect
            logger.info("Reconnecting...");
            idfxx::wifi::try_connect();
        });

        loop.listener_add(idfxx::wifi::sta_got_ip, [](const idfxx::wifi::got_ip_info& info) {
            logger.info("Got IP address: {}", info.ip);
        });

        // --- Initialize and connect ---
        idfxx::wifi::init();
        idfxx::wifi::set_mode(idfxx::wifi::mode::sta);
        idfxx::wifi::set_sta_config({
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .auth_threshold = idfxx::wifi::auth_mode::wpa2_psk,
        });
        idfxx::wifi::start();
        idfxx::wifi::connect();

        logger.info("WiFi station started, connecting to {}...", WIFI_SSID);

        // --- Main loop ---
        while (true) {
            idfxx::delay(1h);
        }

    } catch (const std::system_error& e) {
        logger.error("WiFi error: {}", e.what());
    }
}
