// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/http/server>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <atomic>
#include <chrono>
#include <format>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static std::atomic<uint32_t> request_count{0};

static void register_routes(idfxx::http::server& srv) {
    // GET /api/status — JSON status response
    srv.on_get("/api/status", [](idfxx::http::request& req) -> idfxx::result<void> {
        auto count = request_count.fetch_add(1) + 1;
        req.set_content_type("application/json");
        req.send(std::format(R"({{"status":"ok","requests":{}}})", count));
        return {};
    });

    // GET /hello?name=... — greeting using a query parameter
    srv.on_get("/hello", [](idfxx::http::request& req) -> idfxx::result<void> {
        auto name = req.query_param("name").value_or("world");
        req.set_content_type("text/plain");
        req.send(std::format("Hello, {}!\n", name));
        return {};
    });

    // POST /echo — echoes the request body back
    srv.on_post("/echo", [](idfxx::http::request& req) -> idfxx::result<void> {
        auto body = req.recv_body();
        logger.info("Echoing {} bytes", body.size());
        req.set_content_type("text/plain");
        req.send(body);
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
            logger.info("Open http://{}/hello?name=esp32 in your browser", info.ip4.ip);
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

        // --- Start HTTP server (listens on all interfaces once up) ---
        idfxx::http::server srv({.server_port = 80});
        register_routes(srv);
        logger.info("HTTP server started on port 80");

        // --- Server runs until srv goes out of scope ---
        while (true) {
            idfxx::delay(1h);
        }

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }
}
