// SPDX-License-Identifier: Apache-2.0

#include <idfxx/crypto/hash>
#include <idfxx/event>
#include <idfxx/hex>
#include <idfxx/http/client>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <esp_crt_bundle.h>
#include <span>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static constexpr auto DOWNLOAD_URL = "https://httpbin.org/image/png";

extern "C" void app_main() {
    try {
        // --- Connect to WiFi ---
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto sta_netif = idfxx::wifi::make_sta_netif();
        idfxx::wifi::init();

        logger.info("Connecting to {}...", WIFI_SSID);
        auto ip = idfxx::wifi::connect_sta(
            {
                .ssid = WIFI_SSID,
                .password = WIFI_PASSWORD,
                .auth_threshold = idfxx::wifi::auth_mode::wpa2_psk,
            },
            30s
        );
        logger.info("Got IP: {}", ip.ip);

        // --- Download, hashing the body as it arrives ---
        idfxx::http::client c({
            .url = DOWNLOAD_URL,
            .crt_bundle_attach = esp_crt_bundle_attach,
        });

        logger.info("GET {}", DOWNLOAD_URL);
        c.open();
        c.fetch_headers();
        if (int status = c.status_code(); status != 200) {
            // The body is an error page, not the file
            logger.error("Download failed: HTTP status {}", status);
        } else {
            idfxx::crypto::hash::sha256 hasher;
            size_t length = 0;
            std::array<uint8_t, 1024> buf;
            while (size_t n = c.read(buf)) {
                hasher.update(std::span{buf}.first(n));
                length += n;
            }
            auto digest = hasher.finish();
            logger.info("SHA-256 of {} bytes: {}", length, idfxx::hex(digest));
        }
        c.close();

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    // Idle so the example keeps running
    while (true) {
        idfxx::delay(1h);
    }
}
