// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/http/server>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>
#include <format>
#include <map>
#include <string>
#include <string_view>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static const std::map<std::string, std::string, std::less<>> items{
    {"apple", "A crunchy red fruit.\n"},
    {"banana", "A long yellow fruit.\n"},
};

static void register_error_handlers(idfxx::http::server& srv) {
    // A handler registered with on_error() renders the response for that status, both for errors
    // the server raises itself (an unknown URI) and for ones the app raises with handle_error().
    // Returning success keeps the connection open; returning an error would close it.
    srv.on_error(
        idfxx::http::status_code::not_found,
        [](idfxx::http::request& req, idfxx::http::status_code) -> idfxx::result<void> {
            req.set_status(404);
            req.set_content_type("text/html");
            req.send(std::format("<h1>Not found</h1><p>Nothing lives at <code>{}</code>.</p>", req.uri()));
            return {};
        }
    );

    // httpd itself detects this one (a registered URI requested with the wrong method), so on_error()
    // also customises errors the server raises on its own.
    srv.on_error(
        idfxx::http::status_code::method_not_allowed,
        [](idfxx::http::request& req, idfxx::http::status_code) -> idfxx::result<void> {
            req.set_status(405);
            req.set_content_type("text/plain");
            req.send("That method is not allowed here.\n");
            return {};
        }
    );
}

static void register_routes(idfxx::http::server& srv) {
    // GET / — lists the URLs to try
    srv.on_get("/", [](idfxx::http::request& req) -> idfxx::result<void> {
        req.set_content_type("text/plain");
        req.send(
            "Try:\n"
            "  curl -i http://<ip>/items/apple      # found\n"
            "  curl -i http://<ip>/items/durian     # app-level miss: custom 404 page\n"
            "  curl -i http://<ip>/missing          # server-detected miss: same 404 page\n"
            "  curl -i -X POST http://<ip>/         # wrong method: custom 405\n"
        );
        return {};
    });

    // GET /items/<name> — looks up an item (needs wildcard_uri_match)
    srv.on_get("/items/*", [](idfxx::http::request& req) -> idfxx::result<void> {
        auto path = req.uri().substr(0, req.uri().find('?'));
        auto name = path.substr(std::string_view{"/items/"}.size());
        auto it = items.find(name);
        if (it == items.end()) {
            // Dispatches to the on_error(not_found) handler, so this miss renders the same page
            req.handle_error(idfxx::http::status_code::not_found);
            return {};
        }
        req.set_content_type("text/plain");
        req.send(it->second);
        return {};
    });
}

extern "C" void app_main() {
    try {
        // --- Prerequisites ---
        idfxx::event_loop::create_system();
        idfxx::netif::init();
        auto sta_netif = idfxx::wifi::make_sta_netif();

        // --- Register event listeners ---
        auto& loop = idfxx::event_loop::system();

        loop.listener_add(idfxx::netif::sta_got_ip4, [](const idfxx::netif::ip4_event_data& info) {
            logger.info("Got IP: {}", info.ip4.ip);
            logger.info("Open http://{}/ in your browser", info.ip4.ip);
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

        // --- Start HTTP server (listens on all interfaces once up) ---
        idfxx::http::server srv({.server_port = 80, .wildcard_uri_match = true});
        register_error_handlers(srv);
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
