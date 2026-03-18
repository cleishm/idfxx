// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/wifi>

#include <esp_netif.h>
#include <esp_wifi_default.h>

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        esp_netif_init();
        esp_netif_create_default_wifi_sta();

        // --- Initialize and start WiFi in STA mode ---
        idfxx::wifi::init();
        idfxx::wifi::set_mode(idfxx::wifi::mode::sta);
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
