// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "conversions.hpp"

#include <idfxx/netif>

#include <cstring>
#include <esp_idf_version.h>
#include <esp_netif.h>
#include <esp_netif_ip_addr.h>
#include <esp_netif_types.h>
#include <utility>

// =============================================================================
// Verify enum values match ESP-IDF constants
// =============================================================================

// dhcp_option_id
static_assert(std::to_underlying(idfxx::netif::dhcp_option_id::subnet_mask) == ESP_NETIF_SUBNET_MASK);
static_assert(std::to_underlying(idfxx::netif::dhcp_option_id::domain_name_server) == ESP_NETIF_DOMAIN_NAME_SERVER);
static_assert(
    std::to_underlying(idfxx::netif::dhcp_option_id::router_solicitation_address) ==
    ESP_NETIF_ROUTER_SOLICITATION_ADDRESS
);
static_assert(std::to_underlying(idfxx::netif::dhcp_option_id::requested_ip_address) == ESP_NETIF_REQUESTED_IP_ADDRESS);
static_assert(
    std::to_underlying(idfxx::netif::dhcp_option_id::ip_address_lease_time) == ESP_NETIF_IP_ADDRESS_LEASE_TIME
);
static_assert(
    std::to_underlying(idfxx::netif::dhcp_option_id::ip_request_retry_time) == ESP_NETIF_IP_REQUEST_RETRY_TIME
);
static_assert(
    std::to_underlying(idfxx::netif::dhcp_option_id::vendor_class_identifier) == ESP_NETIF_VENDOR_CLASS_IDENTIFIER
);
static_assert(std::to_underlying(idfxx::netif::dhcp_option_id::vendor_specific_info) == ESP_NETIF_VENDOR_SPECIFIC_INFO);
static_assert(std::to_underlying(idfxx::netif::dhcp_option_id::captiveportal_uri) == ESP_NETIF_CAPTIVEPORTAL_URI);

// dns_type
static_assert(std::to_underlying(idfxx::netif::dns_type::main) == ESP_NETIF_DNS_MAIN);
static_assert(std::to_underlying(idfxx::netif::dns_type::backup) == ESP_NETIF_DNS_BACKUP);
static_assert(std::to_underlying(idfxx::netif::dns_type::fallback) == ESP_NETIF_DNS_FALLBACK);

// flag
static_assert(std::to_underlying(idfxx::netif::flag::dhcp_client) == ESP_NETIF_DHCP_CLIENT);
static_assert(std::to_underlying(idfxx::netif::flag::dhcp_server) == ESP_NETIF_DHCP_SERVER);
static_assert(std::to_underlying(idfxx::netif::flag::autoup) == ESP_NETIF_FLAG_AUTOUP);
static_assert(std::to_underlying(idfxx::netif::flag::garp) == ESP_NETIF_FLAG_GARP);
static_assert(std::to_underlying(idfxx::netif::flag::event_ip_modified) == ESP_NETIF_FLAG_EVENT_IP_MODIFIED);
static_assert(std::to_underlying(idfxx::netif::flag::is_ppp) == ESP_NETIF_FLAG_IS_PPP);
static_assert(std::to_underlying(idfxx::netif::flag::is_bridge) == ESP_NETIF_FLAG_IS_BRIDGE);
static_assert(std::to_underlying(idfxx::netif::flag::mldv6_report) == ESP_NETIF_FLAG_MLDV6_REPORT);
static_assert(
    std::to_underlying(idfxx::netif::flag::ipv6_autoconfig_enabled) == ESP_NETIF_FLAG_IPV6_AUTOCONFIG_ENABLED
);

// ip_event_id
static_assert(std::to_underlying(idfxx::netif::ip_event_id::sta_got_ip4) == IP_EVENT_STA_GOT_IP);
static_assert(std::to_underlying(idfxx::netif::ip_event_id::sta_lost_ip4) == IP_EVENT_STA_LOST_IP);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
static_assert(std::to_underlying(idfxx::netif::ip_event_id::ap_sta_ip4_assigned) == IP_EVENT_ASSIGNED_IP_TO_CLIENT);
#else
static_assert(std::to_underlying(idfxx::netif::ip_event_id::ap_sta_ip4_assigned) == IP_EVENT_AP_STAIPASSIGNED);
#endif
static_assert(std::to_underlying(idfxx::netif::ip_event_id::got_ip6) == IP_EVENT_GOT_IP6);
static_assert(std::to_underlying(idfxx::netif::ip_event_id::eth_got_ip4) == IP_EVENT_ETH_GOT_IP);
static_assert(std::to_underlying(idfxx::netif::ip_event_id::eth_lost_ip4) == IP_EVENT_ETH_LOST_IP);
static_assert(std::to_underlying(idfxx::netif::ip_event_id::ppp_got_ip4) == IP_EVENT_PPP_GOT_IP);
static_assert(std::to_underlying(idfxx::netif::ip_event_id::ppp_lost_ip4) == IP_EVENT_PPP_LOST_IP);

// errc
static_assert(std::to_underlying(idfxx::netif::errc::invalid_params) == ESP_ERR_ESP_NETIF_INVALID_PARAMS);
static_assert(std::to_underlying(idfxx::netif::errc::if_not_ready) == ESP_ERR_ESP_NETIF_IF_NOT_READY);
static_assert(std::to_underlying(idfxx::netif::errc::dhcpc_start_failed) == ESP_ERR_ESP_NETIF_DHCPC_START_FAILED);
static_assert(std::to_underlying(idfxx::netif::errc::dhcp_already_started) == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED);
static_assert(std::to_underlying(idfxx::netif::errc::dhcp_already_stopped) == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED);
static_assert(std::to_underlying(idfxx::netif::errc::no_mem) == ESP_ERR_ESP_NETIF_NO_MEM);
static_assert(std::to_underlying(idfxx::netif::errc::dhcp_not_stopped) == ESP_ERR_ESP_NETIF_DHCP_NOT_STOPPED);
static_assert(std::to_underlying(idfxx::netif::errc::driver_attach_failed) == ESP_ERR_ESP_NETIF_DRIVER_ATTACH_FAILED);
static_assert(std::to_underlying(idfxx::netif::errc::init_failed) == ESP_ERR_ESP_NETIF_INIT_FAILED);
static_assert(std::to_underlying(idfxx::netif::errc::dns_not_configured) == ESP_ERR_ESP_NETIF_DNS_NOT_CONFIGURED);
static_assert(std::to_underlying(idfxx::netif::errc::mld6_failed) == ESP_ERR_ESP_NETIF_MLD6_FAILED);
static_assert(std::to_underlying(idfxx::netif::errc::ip6_addr_failed) == ESP_ERR_ESP_NETIF_IP6_ADDR_FAILED);
static_assert(std::to_underlying(idfxx::netif::errc::dhcps_start_failed) == ESP_ERR_ESP_NETIF_DHCPS_START_FAILED);
static_assert(std::to_underlying(idfxx::netif::errc::tx_failed) == ESP_ERR_ESP_NETIF_TX_FAILED);

// =============================================================================
// Event base definition
// =============================================================================

namespace idfxx::netif {

const event_base<ip_event_id> ip_events{IP_EVENT};

} // namespace idfxx::netif

// =============================================================================
// Error category
// =============================================================================

namespace idfxx {

const netif::error_category& netif_category() noexcept {
    static const netif::error_category instance{};
    return instance;
}

const char* netif::error_category::name() const noexcept {
    return "netif::Error";
}

std::string netif::error_category::message(int ec) const {
    switch (netif::errc(ec)) {
    case netif::errc::invalid_params:
        return "Invalid parameters";
    case netif::errc::if_not_ready:
        return "Interface not ready";
    case netif::errc::dhcpc_start_failed:
        return "DHCP client start failed";
    case netif::errc::dhcp_already_started:
        return "DHCP already started";
    case netif::errc::dhcp_already_stopped:
        return "DHCP already stopped";
    case netif::errc::no_mem:
        return "Out of memory";
    case netif::errc::dhcp_not_stopped:
        return "DHCP not stopped";
    case netif::errc::driver_attach_failed:
        return "Driver attach failed";
    case netif::errc::init_failed:
        return "Initialization failed";
    case netif::errc::dns_not_configured:
        return "DNS not configured";
    case netif::errc::mld6_failed:
        return "MLD6 operation failed";
    case netif::errc::ip6_addr_failed:
        return "IPv6 address operation failed";
    case netif::errc::dhcps_start_failed:
        return "DHCP server start failed";
    case netif::errc::tx_failed:
        return "Transmit failed";
    default:
        return esp_err_to_name(static_cast<esp_err_t>(ec));
    }
}

// =============================================================================
// Netif error helper
// =============================================================================

static bool is_netif_error(esp_err_t e) noexcept {
    return (e & 0xFF00) == ESP_ERR_ESP_NETIF_BASE;
}

std::unexpected<std::error_code> netif_error(esp_err_t e) {
    if (e == ESP_ERR_NO_MEM) {
        raise_no_mem();
    }
    if (is_netif_error(e)) {
        return std::unexpected(std::error_code{e, netif_category()});
    }
    return error(e);
}

// =============================================================================
// Verify from_opaque exists on all event data types whose layout differs from ESP-IDF
// =============================================================================

// clang-format off
static_assert(requires(const void* p) { { netif::ip4_event_data::from_opaque(p) } -> std::same_as<netif::ip4_event_data>; },
    "ip4_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { netif::ip6_event_data::from_opaque(p) } -> std::same_as<netif::ip6_event_data>; },
    "ip6_event_data requires from_opaque — layout differs from ESP-IDF struct");
static_assert(requires(const void* p) { { netif::ap_sta_ip4_assigned_event_data::from_opaque(p) } -> std::same_as<netif::ap_sta_ip4_assigned_event_data>; },
    "ap_sta_ip4_assigned_event_data requires from_opaque — layout differs from ESP-IDF struct");
// clang-format on

} // namespace idfxx

// =============================================================================
// Event data wrapper implementations
// =============================================================================

namespace idfxx::netif {

ip4_event_data ip4_event_data::from_opaque(const void* event_data) {
    auto* data = static_cast<const ip_event_got_ip_t*>(event_data);
    return {
        .ip4 = detail::ip4_from_native(data->ip_info),
        .changed = data->ip_changed,
    };
}

ip6_event_data ip6_event_data::from_opaque(const void* event_data) {
    auto* data = static_cast<const ip_event_got_ip6_t*>(event_data);
    return {
        .ip = detail::ip6_from_native(data->ip6_info.ip),
        .index = data->ip_index,
    };
}

ap_sta_ip4_assigned_event_data ap_sta_ip4_assigned_event_data::from_opaque(const void* event_data) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    auto* data = static_cast<const ip_event_assigned_ip_to_client_t*>(event_data);
#else
    auto* data = static_cast<const ip_event_ap_staipassigned_t*>(event_data);
#endif
    ap_sta_ip4_assigned_event_data result;
    result.ip = net::ip4_addr(data->ip.addr);
    std::memcpy(result.mac.data(), data->mac, 6);
    return result;
}

} // namespace idfxx::netif

// =============================================================================
// String conversions
// =============================================================================

namespace idfxx {

std::string to_string(netif::dns_type t) {
    switch (t) {
    case netif::dns_type::main:
        return "main";
    case netif::dns_type::backup:
        return "backup";
    case netif::dns_type::fallback:
        return "fallback";
    default:
        return "unknown(" + std::to_string(static_cast<int>(t)) + ")";
    }
}

} // namespace idfxx
