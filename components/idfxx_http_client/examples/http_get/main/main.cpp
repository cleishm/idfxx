// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/http/client>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <esp_crt_bundle.h>
#include <string>
#include <string_view>

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
        auto sta_netif = idfxx::wifi::make_sta_netif();

        idfxx::event_group<sync_flag> network_ready;

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::netif::sta_got_ip4, [&](const idfxx::netif::ip4_event_data& info) {
            logger.info("Got IP: {}", info.ip4.ip);
            network_ready.set(sync_flag::got_ip);
        });

        loop.listener_add(idfxx::wifi::sta_disconnected, [](const idfxx::wifi::disconnected_event_data& info) {
            logger.warn("Disconnected (reason: {}), reconnecting...", info.reason);
            if (auto r = idfxx::wifi::try_connect(); !r) {
                logger.error("Reconnect failed: {}", r.error().message());
            }
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
        network_ready.wait(sync_flag::got_ip, idfxx::wait_mode::any);

        // --- Perform HTTPS GET ---
        std::string body;
        std::string content_type;
        idfxx::http::client c({
            .url = REQUEST_URL,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .on_event = [&](const idfxx::http::event_data& evt) {
                // Redirects and auth retries send a new request; keep only the final response.
                if (evt.id == idfxx::http::event_id::headers_sent) {
                    content_type.clear();
                    body.clear();
                } else if (evt.id == idfxx::http::event_id::on_header &&
                           std::ranges::equal(evt.header_key, std::string_view("Content-Type"), [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) ==
                                   std::tolower(static_cast<unsigned char>(b));
                           })) {
                    content_type = evt.header_value;
                } else if (evt.id == idfxx::http::event_id::on_data) {
                    body.append(reinterpret_cast<const char*>(evt.data.data()), evt.data.size());
                }
            },
        });

        logger.info("GET {}", REQUEST_URL);
        c.perform();

        logger.info("Status: {}", c.status_code());
        if (!content_type.empty()) {
            logger.info("Content-Type: {}", content_type);
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
