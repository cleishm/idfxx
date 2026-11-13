// SPDX-License-Identifier: Apache-2.0

#include <idfxx/crypto/mac>
#include <idfxx/event>
#include <idfxx/http/server>
#include <idfxx/log>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network and the secret shared with your webhook senders. A real
// application shouldn't build secrets into its firmware: provision them per device, for example
// into encrypted NVS.
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";
static constexpr std::string_view WEBHOOK_SECRET = "change-me";

using signature = idfxx::crypto::mac::hmac_sha256::tag_type;

// Decodes an X-Signature header value: "sha256=" followed by the tag as 64 hex digits.
static std::optional<signature> parse_signature(std::string_view header) {
    constexpr std::string_view prefix = "sha256=";
    signature sig;
    if (!header.starts_with(prefix) || header.size() != prefix.size() + 2 * sig.size()) {
        return std::nullopt;
    }
    const char* hex = header.data() + prefix.size();
    for (std::byte& b : sig) {
        uint8_t value;
        auto [end, ec] = std::from_chars(hex, hex + 2, value, 16);
        if (ec != std::errc{} || end != hex + 2) {
            return std::nullopt;
        }
        b = std::byte{value};
        hex += 2;
    }
    return sig;
}

// The response to a request whose body couldn't be read or authenticated.
static idfxx::http::error_status failure_status(std::error_code ec) {
    if (ec == idfxx::errc::timeout) {
        return idfxx::http::status_code::request_timeout; // the body stopped arriving
    }
    if (ec == idfxx::http::server::errc::invalid_request) {
        return idfxx::http::status_code::bad_request; // the body ended early
    }
    return idfxx::http::status_code::internal_server_error;
}

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

        idfxx::event_loop::system().listener_add(
            idfxx::wifi::sta_disconnected, [](const idfxx::wifi::disconnected_event_data& info) {
                logger.warn("Disconnected (reason: {}), reconnecting...", info.reason);
                if (auto r = idfxx::wifi::try_connect(); !r) {
                    logger.error("Reconnect failed: {}", r.error().message());
                }
            }
        );

        // --- Import the shared secret as an HMAC-SHA-256 key ---
        auto secret =
            idfxx::crypto::mac::hmac_sha256::import_key(std::as_bytes(std::span{WEBHOOK_SECRET}));

        // --- Accept webhooks whose body is signed with the secret ---
        idfxx::http::server srv({
            .stack_size = 6144, // room for the read buffer and the HMAC computation
        });

        srv.on_post("/hook", [&secret](idfxx::http::request& req) -> idfxx::result<void> {
            auto expected = parse_signature(req.header("X-Signature").value_or(""));
            if (!expected) {
                logger.warn("Rejected: missing or malformed X-Signature header");
                req.send_error(401, "Missing or malformed signature");
                return {};
            }

            // Authenticate the body as it arrives, so it never has to fit in memory
            bool authentic;
            try {
                idfxx::crypto::mac::hmac_sha256 hmac{secret};
                std::array<std::byte, 512> buf;
                while (size_t n = req.read(buf)) {
                    hmac.update(std::span{buf}.first(n));
                }
                authentic = hmac.verify(*expected);
            } catch (const std::system_error& e) {
                // Reading or authenticating the body failed. If even the error response can't be
                // sent, handle_error() throws, and the server closes the connection without one.
                logger.error("Failed to check the request: {}", e.what());
                req.handle_error(failure_status(e.code()));
                return {};
            }

            if (!authentic) {
                logger.warn("Rejected: signature does not match the body");
                req.send_error(401, "Bad signature");
                return {};
            }

            logger.info("Accepted a webhook of {} bytes", req.content_length());
            req.send("OK\n");
            return {};
        });

        logger.info("Listening for webhooks at http://{}/hook", ip.ip);

        // --- Server runs until srv goes out of scope ---
        while (true) {
            idfxx::delay(1h);
        }

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }
}
