// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/wifi>

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto sta_netif = idfxx::wifi::create_default_sta_netif();

        // --- Initialize and start WiFi in STA mode ---
        idfxx::wifi::init();
        idfxx::wifi::set_roles(idfxx::wifi::role::sta);
        idfxx::wifi::start();

        // --- Perform a blocking scan ---
        logger.info("Starting WiFi scan...");
        auto results = idfxx::wifi::scan();
        logger.info("Scan complete: found {} access point(s)", results.size());

        // --- Display results ---
        for (const auto& ap : results) {
            logger.info("  {:32s}  ch:{:2d}  rssi:{:4d}  {}", ap.ssid, ap.primary_channel, ap.rssi, ap.authmode);
        }

        // --- Cleanup ---
        idfxx::wifi::stop();
        idfxx::wifi::deinit();

    } catch (const std::system_error& e) {
        logger.error("WiFi error: {}", e.what());
    }
}
