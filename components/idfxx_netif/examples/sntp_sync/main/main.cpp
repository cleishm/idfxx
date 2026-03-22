// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>
#include <ctime>

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

        // --- Initialize SNTP (before connecting, so it starts once IP is obtained) ---
        idfxx::netif::sntp::init({
            .smooth_sync = false,
            .wait_for_sync = true,
            .start = true,
            .servers = {"pool.ntp.org", "time.google.com"},
        });

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::netif::sta_got_ip4, [](const idfxx::netif::ip4_event_data& info) {
            logger.info("Got IP: {}", info.ip4.ip);
        });

        loop.listener_add(idfxx::wifi::sta_disconnected, [](const idfxx::wifi::disconnected_event_data& info) {
            logger.warn("Disconnected (reason: {}), reconnecting...", info.reason);
            idfxx::wifi::try_connect();
        });

        // --- Connect to WiFi ---
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

        // --- Wait for time synchronization ---
        logger.info("Waiting for SNTP time sync...");
        if (idfxx::netif::sntp::sync_wait(30s)) {
            logger.info("Time synchronized!");
        } else {
            logger.warn("SNTP sync timed out after 30s");
        }

        // --- Display current time periodically ---
        while (true) {
            auto now = std::time(nullptr);
            auto* tm = std::localtime(&now);
            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
            logger.info("Current time: {} (UTC)", buf);

            idfxx::delay(10s);
        }

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }
}
