// SPDX-License-Identifier: Apache-2.0
//
// Sends an ICMP echo request and waits for the reply, using a raw IPv4
// socket. Demonstrates `raw_socket::send_to` / `recv_from` and the standard
// Internet checksum.

#include <idfxx/event>
#include <idfxx/event_group>
#include <idfxx/log>
#include <idfxx/net/raw_socket>
#include <idfxx/net/resolver>
#include <idfxx/netif>
#include <idfxx/sched>
#include <idfxx/wifi>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <lwip/sockets.h>

using namespace std::chrono_literals;

static constexpr idfxx::log::logger logger{"example"};

// Change these to match your network
static constexpr auto WIFI_SSID = "MyNetwork";
static constexpr auto WIFI_PASSWORD = "MyPassword";

static constexpr auto TARGET_HOST = "8.8.8.8";

enum class sync_flag : uint32_t {
    got_ip = 1u << 0,
};

template<>
inline constexpr bool idfxx::enable_flags_operators<sync_flag> = true;

namespace {
uint16_t internet_checksum(std::span<const std::byte> data) noexcept;
idfxx::net::ipv4_addr connect_to_wifi();
} // namespace

extern "C" void app_main() {
    try {
        connect_to_wifi();

        // --- Resolve target ---
        auto target =
            idfxx::net::resolve_one(TARGET_HOST, 0, {.family = idfxx::net::address_family::ipv4, .numeric_host = true});
        logger.info("Pinging {}", target);

        // --- Open raw ICMP socket ---
        idfxx::net::raw_socket sock(idfxx::net::ip_protocol::icmp, idfxx::net::address_family::ipv4);
        sock.set_recv_timeout(5s);

        // --- Build ICMP echo request: 8-byte header + 32-byte payload ---
        std::array<std::byte, 40> packet{};
        packet[0] = std::byte{8}; // type = echo request
        packet[1] = std::byte{0}; // code
        // bytes [2..3] = checksum (computed below; must start as zero)
        const uint16_t identifier = htons(0x1234);
        const uint16_t sequence = htons(1);
        std::memcpy(packet.data() + 4, &identifier, sizeof(identifier));
        std::memcpy(packet.data() + 6, &sequence, sizeof(sequence));
        // payload bytes [8..40) left as zero
        const uint16_t cksum = internet_checksum(packet);
        std::memcpy(packet.data() + 2, &cksum, sizeof(cksum));

        // --- Send and wait for reply ---
        auto t0 = std::chrono::steady_clock::now();
        sock.send_to(packet, target);

        // For IPv4 raw sockets, lwIP delivers the IP header in front of the
        // ICMP payload. The IHL field (low nibble of byte 0) gives its length
        // in 32-bit words.
        std::array<std::byte, 256> reply_buf;
        auto reply = sock.recv_from(reply_buf);
        auto rtt = std::chrono::steady_clock::now() - t0;
        auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(rtt).count();

        const auto ip_hdr_len = (static_cast<uint8_t>(reply.data[0]) & 0x0f) * 4;
        if (reply.data.size() < static_cast<size_t>(ip_hdr_len) + 8) {
            logger.warn("Reply from {} too short ({} bytes)", reply.from, reply.data.size());
        } else {
            const auto icmp_type = static_cast<uint8_t>(reply.data[ip_hdr_len]);
            if (icmp_type == 0) { // echo reply
                logger.info("Reply from {}: {} bytes, rtt = {} us", reply.from, reply.data.size() - ip_hdr_len, rtt_us);
            } else {
                logger.warn("Unexpected ICMP type {} from {}", icmp_type, reply.from);
            }
        }

    } catch (const std::system_error& e) {
        logger.error("Error: {}", e.what());
    }

    while (true) {
        idfxx::delay(1h);
    }
}

namespace {

// 16-bit ones-complement Internet checksum per RFC 1071. Returns the value
// already in network byte order, ready to be stored into the checksum field.
uint16_t internet_checksum(std::span<const std::byte> data) noexcept {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < data.size(); i += 2) {
        sum += (static_cast<uint16_t>(data[i]) << 8) | static_cast<uint16_t>(data[i + 1]);
    }
    if (data.size() % 2 != 0) {
        sum += static_cast<uint16_t>(data[data.size() - 1]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return htons(static_cast<uint16_t>(~sum));
}

// Brings up the WiFi STA, blocks until DHCP assigns an IPv4 address, and
// registers an auto-reconnect listener for the rest of the program's lifetime.
// Returns the assigned address. Call once.
idfxx::net::ipv4_addr connect_to_wifi() {
    idfxx::event_loop::create_system();
    idfxx::netif::init();
    static auto sta_netif = idfxx::wifi::create_default_sta_netif();
    static idfxx::event_group<sync_flag> network_ready;
    static idfxx::net::ipv4_addr my_ip{};

    auto& loop = idfxx::event_loop::system();

    loop.listener_add(idfxx::netif::sta_got_ip4, [](const idfxx::netif::ip4_event_data& info) {
        my_ip = info.ip4.ip;
        logger.info("Got IP: {}", info.ip4.ip);
        network_ready.set(sync_flag::got_ip);
    });

    loop.listener_add(idfxx::wifi::sta_disconnected, [](const idfxx::wifi::disconnected_event_data& info) {
        logger.warn("Disconnected (reason: {}), reconnecting...", info.reason);
        // Listener runs in the event-loop trampoline (C frame); exceptions
        // must not escape, so suppress at this boundary.
        try {
            idfxx::wifi::connect();
        } catch (const std::system_error& e) {
            logger.warn("reconnect failed: {}", e.what());
        }
    });

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
    network_ready.wait(sync_flag::got_ip);

    return my_ip;
}

} // namespace
