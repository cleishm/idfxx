// SPDX-License-Identifier: Apache-2.0

#include <idfxx/event>
#include <idfxx/log>
#include <idfxx/mdns>
#include <idfxx/netif>
#include <idfxx/nvs>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <chrono>
#include <string_view>
#include <system_error>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

// The device answers as <HOSTNAME>.local
static constexpr auto HOSTNAME = "idfxx-mdns";

static void log_answer(std::string_view source, const idfxx::mdns::answer& a) {
    logger.info(
        "{}: '{}' on {}.local port {}, TTL {}s",
        source,
        a.instance_name.value_or("?"),
        a.hostname.value_or("?"),
        a.port.value_or(0),
        a.ttl.count()
    );
    for (const auto& addr : a.ipv4_addresses) {
        logger.info("  address {}", addr);
    }
    for (const auto& item : a.txt) {
        logger.info("  txt {}={}", item.key, item.value.value_or("(no value)"));
    }
}

extern "C" void app_main() {
    try {
        // --- Join the Wi-Fi network ---
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
        idfxx::wifi::init();
        auto ip = idfxx::wifi::connect_sta(
            {.ssid = WIFI_SSID,
             .password = WIFI_PASSWORD,
             .auth_threshold = idfxx::wifi::auth_mode::wpa2_psk},
            30s
        );
        logger.info("Connected with IP address {}", ip.ip);

        // --- Start mDNS and claim a hostname ---
        idfxx::event_loop::system().listener_add(
            idfxx::mdns::hostname_changed, [](const idfxx::mdns::hostname_info& info) {
                logger.info("Answering as {}.local", info.hostname);
            }
        );
        idfxx::mdns::init();
        idfxx::mdns::set_hostname(HOSTNAME);

        // --- Advertise a web service ---
        idfxx::mdns::service web({
            .instance_name = "idfxx mDNS example",
            .type = "_http",
            .protocol = idfxx::mdns::protocol::tcp,
            .port = 80,
            .txt = {{.key = "path", .value = "/"}, {.key = "board", .value = CONFIG_IDF_TARGET}},
        });
        logger.info("Advertising _http._tcp on port 80");

        // --- Follow other web services as they come and go ---
        // The callback runs on the mDNS task: it only logs, and never calls mDNS.
        idfxx::mdns::browser peers(
            {.type = "_http", .protocol = idfxx::mdns::protocol::tcp},
            [](const idfxx::mdns::answer& a) { log_answer(a.ttl == 0s ? "gone" : "browse", a); }
        );

        // --- Ask once, and wait for the answers ---
        auto found = idfxx::mdns::query_ptr("_http", idfxx::mdns::protocol::tcp, 3s);
        logger.info("Query found {} web service(s)", found.size());
        for (const auto& a : found) {
            log_answer("query", a);
        }

        while (true) {
            idfxx::delay(1h);
        }
    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }
}
