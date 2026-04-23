// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/http/client>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>
#include <cstdint>
#include <esp_crt_bundle.h>
#include <string>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static constexpr auto REQUEST_URL = "https://httpbin.org/get";

enum class sync_flag : uint32_t {
    got_ip = 1u << 0,
};

template<>
inline constexpr bool idfxx::enable_flags_operators<sync_flag> = true;

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto sta_netif = idfxx::wifi::create_default_sta_netif();

        idfxx::event_group<sync_flag> network_ready;

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::netif::sta_got_ip4, [&](const idfxx::netif::ip4_event_data& info) {
            logger.info("Got IP: {}", info.ip4.ip);
            network_ready.set(sync_flag::got_ip);
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
        (void)network_ready.wait(sync_flag::got_ip, idfxx::wait_mode::any);

        // --- Perform HTTPS GET ---
        std::string body;
        idfxx::http::client c({
            .url = REQUEST_URL,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .on_event = [&](const idfxx::http::event_data& evt) {
                if (evt.id == idfxx::http::event_id::on_data) {
                    body.append(reinterpret_cast<const char*>(evt.data.data()), evt.data.size());
                }
            },
        });

        logger.info("GET {}", REQUEST_URL);
        c.perform();

        logger.info("Status: {}", c.status_code());
        if (auto ctype = c.get_header("Content-Type")) {
            logger.info("Content-Type: {}", *ctype);
        }
        logger.info("Body ({} bytes): {}", body.size(), body);

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    // Idle so the example keeps running
    while (true) {
        idfxx::delay(1h);
    }
}
