// SPDX-License-Identifier: Apache-2.0

#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/nvs>
#include <idfxx/wifi>

static constexpr idfxx::log::logger logger{"example"};

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        // Wi-Fi stores its settings in NVS. Erase NVS if it is full or has a newer format.
        try {
            idfxx::nvs::flash::init();
        } catch (const std::system_error& e) {
            if (e.code() != idfxx::nvs::errc::no_free_pages &&
                e.code() != idfxx::nvs::errc::new_version_found) {
                throw;
            }
            idfxx::nvs::flash::erase();
            idfxx::nvs::flash::init();
        }
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto sta_netif = idfxx::wifi::make_sta_netif();

        // --- Initialize and start Wi-Fi in STA mode ---
        idfxx::wifi::init();
        idfxx::wifi::set_roles(idfxx::wifi::role::sta);
        idfxx::wifi::start();

        // --- Perform a blocking scan ---
        logger.info("Starting Wi-Fi scan...");
        auto results = idfxx::wifi::scan();
        logger.info("Scan complete: found {} access point(s)", results.size());

        // --- Display results ---
        for (const auto& ap : results) {
            logger.info(
                "  {:32s}  ch:{:2d}  rssi:{:4.0f}  {}",
                ap.ssid,
                ap.primary_channel,
                ap.rssi,
                ap.authmode
            );
        }

        // --- Cleanup ---
        idfxx::wifi::stop();
        idfxx::wifi::deinit();

    } catch (const std::system_error& e) {
        logger.error("Wi-Fi error: {}", e.what());
    }
}
