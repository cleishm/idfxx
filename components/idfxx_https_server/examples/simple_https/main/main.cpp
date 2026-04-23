// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/http/ssl_server>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>
#include <string>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

// Server certificate and private key are embedded via EMBED_TXTFILES.
// The self-signed certs in certs/ are for demonstration only — replace
// them with your own before deploying.
extern const char server_crt_start[] asm("_binary_server_crt_start");
extern const char server_crt_end[] asm("_binary_server_crt_end");
extern const char server_key_start[] asm("_binary_server_key_start");
extern const char server_key_end[] asm("_binary_server_key_end");

static void register_routes(idfxx::http::server& srv) {
    srv.on_get("/api/status", [](idfxx::http::request& req) -> idfxx::result<void> {
        req.set_content_type("application/json");
        req.send(R"({"status":"ok","secure":true})");
        return {};
    });

    srv.on_get("/hello", [](idfxx::http::request& req) -> idfxx::result<void> {
        req.set_content_type("text/plain");
        req.send("Hello over TLS!\n");
        return {};
    });
}

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto sta_netif = idfxx::wifi::create_default_sta_netif();

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::netif::sta_got_ip4, [](const idfxx::netif::ip4_event_data& info) {
            logger.info("Got IP: {}", info.ip4.ip);
            logger.info("Try: curl --insecure https://{}/hello", info.ip4.ip);
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

        // --- Start HTTPS server ---
        idfxx::http::ssl_server srv({
            .server_port = 443,
            .server_cert = std::string(server_crt_start, server_crt_end),
            .private_key = std::string(server_key_start, server_key_end),
        });
        register_routes(srv);
        logger.info("HTTPS server started on port 443");

        // --- Server runs until srv goes out of scope ---
        while (true) {
            idfxx::delay(1h);
        }

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }
}
