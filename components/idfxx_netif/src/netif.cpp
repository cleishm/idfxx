// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "conversions.hpp"

#include <idfxx/netif>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <limits>
#include <memory>

#ifdef CONFIG_LWIP_DHCPS
#include <dhcpserver/dhcpserver.h>
#endif

namespace idfxx::netif {

using detail::ip4_from_native;
using detail::ip6_from_native;
using detail::to_native;

// =============================================================================
// interface class
// =============================================================================

interface interface::take(esp_netif_t* handle) {
    return take(handle, esp_netif_destroy);
}

interface interface::take(esp_netif_t* handle, destroy_fn destroy) {
    assert(handle != nullptr);
    assert(destroy != nullptr);
    return interface(handle, destroy);
}

interface interface::wrap(esp_netif_t* handle) {
    assert(handle != nullptr);
    return interface(handle, nullptr);
}

void interface::_release() noexcept {
    if (_destroy != nullptr && _handle != nullptr) {
        esp_netif_dhcpc_stop(_handle);
#ifdef CONFIG_LWIP_DHCPS
        esp_netif_dhcps_stop(_handle);
#endif
        _destroy(_handle);
    }
}

interface::~interface() {
    _release();
}

interface& interface::operator=(interface&& other) noexcept {
    if (this != &other) {
        _release();
        _handle = std::exchange(other._handle, nullptr);
        _destroy = std::exchange(other._destroy, nullptr);
#ifdef CONFIG_LWIP_DHCPS
        // Freed only now that the handle that pointed at it is destroyed
        _captive_portal_uri = std::move(other._captive_portal_uri);
#endif
    }
    return *this;
}

// =========================================================================
// Interface status
// =========================================================================

bool interface::is_up() const {
    if (_handle == nullptr) {
        return false;
    }
    return esp_netif_is_netif_up(_handle);
}

std::string_view interface::key() const {
    if (_handle == nullptr) {
        return {};
    }
    const char* key = esp_netif_get_ifkey(_handle);
    return key != nullptr ? std::string_view{key} : std::string_view{};
}

std::string_view interface::description() const {
    if (_handle == nullptr) {
        return {};
    }
    const char* description = esp_netif_get_desc(_handle);
    return description != nullptr ? std::string_view{description} : std::string_view{};
}

int interface::get_route_priority() const {
    if (_handle == nullptr) {
        return -1;
    }
    return esp_netif_get_route_prio(_handle);
}

void interface::set_route_priority(int priority) {
    if (_handle == nullptr) {
        return;
    }
    esp_netif_set_route_prio(_handle, priority);
}

flags<flag> interface::get_flags() const {
    if (_handle == nullptr) {
        return {};
    }
    return flags<flag>(static_cast<flag>(esp_netif_get_flags(_handle)));
}

idfxx::event<ip_event_id, ip4_event_data> interface::got_ip4_event() const {
    if (_handle == nullptr) {
        return {};
    }
    auto id = static_cast<ip_event_id>(esp_netif_get_event_id(_handle, ESP_NETIF_IP_EVENT_GOT_IP));
    return idfxx::event<ip_event_id, ip4_event_data>{id};
}

idfxx::event<ip_event_id> interface::lost_ip4_event() const {
    if (_handle == nullptr) {
        return {};
    }
    auto id = static_cast<ip_event_id>(esp_netif_get_event_id(_handle, ESP_NETIF_IP_EVENT_LOST_IP));
    return idfxx::event<ip_event_id>{id};
}

// =========================================================================
// MAC address
// =========================================================================

result<mac_address> interface::try_get_mac() const {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    uint8_t mac[6];
    auto err = esp_netif_get_mac(_handle, mac);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return mac_address(mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

result<void> interface::try_set_mac(const mac_address& mac) {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    // esp_netif_set_mac takes a non-const pointer, so we need a mutable copy
    uint8_t buf[6];
    std::memcpy(buf, mac.data(), 6);
    auto err = esp_netif_set_mac(_handle, buf);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

// =========================================================================
// Hostname
// =========================================================================

result<std::string> interface::try_get_hostname() const {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    const char* hostname = nullptr;
    auto err = esp_netif_get_hostname(_handle, &hostname);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return std::string(hostname ? hostname : "");
}

result<void> interface::try_set_hostname(std::string_view hostname) {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    std::string h(hostname);
    auto err = esp_netif_set_hostname(_handle, h.c_str());
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

// =========================================================================
// IPv4
// =========================================================================

net::ipv4_info interface::get_ipv4_info() const {
    if (_handle == nullptr) {
        return {};
    }
    esp_netif_ip_info_t native;
    esp_netif_get_ip_info(_handle, &native);
    return ip4_from_native(native);
}

result<void> interface::try_set_ipv4_info(const net::ipv4_info& info) {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    auto native = to_native(info);
    auto err = esp_netif_set_ip_info(_handle, &native);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

// =========================================================================
// IPv6
// =========================================================================

result<void> interface::try_create_ipv6_linklocal() {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
#ifdef CONFIG_LWIP_IPV6
    auto err = esp_netif_create_ip6_linklocal(_handle);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
#else
    return error(idfxx::errc::not_supported);
#endif
}

result<net::ipv6_addr> interface::try_get_ipv6_linklocal() const {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
#ifdef CONFIG_LWIP_IPV6
    esp_ip6_addr_t native;
    auto err = esp_netif_get_ip6_linklocal(_handle, &native);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return ip6_from_native(native);
#else
    return error(idfxx::errc::not_supported);
#endif
}

result<net::ipv6_addr> interface::try_get_ipv6_global() const {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
#ifdef CONFIG_LWIP_IPV6
    esp_ip6_addr_t native;
    auto err = esp_netif_get_ip6_global(_handle, &native);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return ip6_from_native(native);
#else
    return error(idfxx::errc::not_supported);
#endif
}

std::vector<net::ipv6_addr> interface::get_all_ipv6() const {
    if (_handle == nullptr) {
        return {};
    }
#ifdef CONFIG_LWIP_IPV6
    esp_ip6_addr_t addrs[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
    int count = esp_netif_get_all_ip6(_handle, addrs);
    std::vector<net::ipv6_addr> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.push_back(ip6_from_native(addrs[i]));
    }
    return result;
#else
    return {};
#endif
}

std::vector<net::ipv6_addr> interface::get_all_preferred_ipv6() const {
    if (_handle == nullptr) {
        return {};
    }
#ifdef CONFIG_LWIP_IPV6
    esp_ip6_addr_t addrs[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
    int count = esp_netif_get_all_preferred_ip6(_handle, addrs);
    std::vector<net::ipv6_addr> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.push_back(ip6_from_native(addrs[i]));
    }
    return result;
#else
    return {};
#endif
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 1, 0) &&                                             \
    defined(CONFIG_LWIP_ND6_SUPPORT_STATIC_ENTRIES)
result<void>
interface::try_add_static_neighbor(const net::ipv6_addr& addr, const mac_address& mac) {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    auto native = to_native(addr);
    auto err = esp_netif_add_static_neighbor(_handle, &native, mac.data());
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

result<void> interface::try_remove_static_neighbor(const net::ipv6_addr& addr) {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    auto native = to_native(addr);
    auto err = esp_netif_remove_static_neighbor(_handle, &native);
    if (err == ESP_FAIL) {
        return error(idfxx::errc::not_found);
    }
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}
#endif

// =========================================================================
// DNS
// =========================================================================

result<void> interface::try_set_dns(dns_type type, const dns_info& info) {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    esp_netif_dns_info_t native;
    native.ip.type = ESP_IPADDR_TYPE_V4;
    native.ip.u_addr.ip4.addr = info.ip.addr();
    auto err = esp_netif_set_dns_info(
        _handle, static_cast<esp_netif_dns_type_t>(std::to_underlying(type)), &native
    );
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

result<dns_info> interface::try_get_dns(dns_type type) const {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    esp_netif_dns_info_t native;
    auto err = esp_netif_get_dns_info(
        _handle, static_cast<esp_netif_dns_type_t>(std::to_underlying(type)), &native
    );
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return dns_info{.ip = net::ipv4_addr(native.ip.u_addr.ip4.addr)};
}

// =========================================================================
// NAPT
// =========================================================================

result<void> interface::try_napt_enable(bool enable) {
    if (_handle == nullptr) {
        return error(idfxx::errc::invalid_state);
    }
    auto err = enable ? esp_netif_napt_enable(_handle) : esp_netif_napt_disable(_handle);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

// =========================================================================
// DHCP client
// =========================================================================

result<void> interface::try_dhcp_client_start() {
    if (!get_flags().contains(flag::dhcp_client)) {
        return error(idfxx::errc::invalid_state);
    }
    auto err = esp_netif_dhcpc_start(_handle);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

result<void> interface::try_dhcp_client_stop() {
    if (!get_flags().contains(flag::dhcp_client)) {
        return error(idfxx::errc::invalid_state);
    }
    auto err = esp_netif_dhcpc_stop(_handle);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

bool interface::is_dhcp_client_running() const {
    if (!get_flags().contains(flag::dhcp_client)) {
        return false;
    }
    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
    esp_netif_dhcpc_get_status(_handle, &status);
    return status == ESP_NETIF_DHCP_STARTED;
}

// =========================================================================
// DHCP server
// =========================================================================

#ifdef CONFIG_LWIP_DHCPS

namespace {

// Space the DHCP server leaves for the captive-portal URI. It writes every offered option into a
// 312-byte field without bounds checks, and the options other than the URI take up to 63 bytes
// (magic cookie 4, message type 3, subnet mask 6, lease time 6, server ID 6, router 6, DNS 10,
// broadcast 6, MTU 4, router discovery 3, vendor info 8, end 1), plus 2 for the URI's own header.
constexpr size_t max_captive_portal_uri_size = 312 - 63 - 2;

// The lease time in the driver's units of CONFIG_LWIP_DHCPS_LEASE_UNIT seconds, rounded up, or
// nullopt if it is not positive or doesn't fit the driver's 32-bit seconds.
std::optional<dhcps_time_t> lease_units(std::chrono::seconds lease_time) {
    constexpr int64_t unit = CONFIG_LWIP_DHCPS_LEASE_UNIT;
    constexpr int64_t max_units = std::numeric_limits<uint32_t>::max() / unit;
    auto seconds = lease_time.count();
    if (seconds <= 0) {
        return std::nullopt;
    }
    auto units = seconds / unit + (seconds % unit != 0 ? 1 : 0);
    if (units > max_units) {
        return std::nullopt;
    }
    return static_cast<dhcps_time_t>(units);
}

result<void>
set_dhcps_option(esp_netif_t* handle, esp_netif_dhcp_option_id_t id, void* value, uint32_t size) {
    auto err = esp_netif_dhcps_option(handle, ESP_NETIF_OP_SET, id, value, size);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

} // namespace

result<void> interface::try_dhcp_server_start() {
    if (!get_flags().contains(flag::dhcp_server)) {
        return error(idfxx::errc::invalid_state);
    }
    auto err = esp_netif_dhcps_start(_handle);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

result<void> interface::try_dhcp_server_start(const dhcp_server_config& cfg) {
    if (!get_flags().contains(flag::dhcp_server)) {
        return error(idfxx::errc::invalid_state);
    }

    // Check the whole configuration before changing anything
    std::optional<dhcps_time_t> lease_time;
    if (cfg.lease_time) {
        lease_time = lease_units(*cfg.lease_time);
        if (!lease_time) {
            return error(idfxx::errc::invalid_arg);
        }
    }
    std::unique_ptr<char[]> uri;
    if (cfg.captive_portal_uri) {
        const auto& value = *cfg.captive_portal_uri;
        if (value.empty() || value.size() > max_captive_portal_uri_size ||
            value.find('\0') != std::string::npos) {
            return error(idfxx::errc::invalid_arg);
        }
        // The driver keeps the pointer for the life of the handle, which a non-owning interface
        // can't guarantee
        if (_destroy == nullptr) {
            return error(idfxx::errc::invalid_state);
        }
        uri = std::make_unique<char[]>(value.size() + 1);
        std::memcpy(uri.get(), value.c_str(), value.size() + 1);
    }

    if (is_dhcp_server_running()) {
        return error(errc::dhcp_already_started);
    }

    // The lease range goes first, as it is the only option the driver validates
    if (cfg.leases) {
        dhcps_lease_t range{};
        range.enable = true;
        range.start_ip.addr = cfg.leases->first.addr();
        range.end_ip.addr = cfg.leases->last.addr();
        auto r = set_dhcps_option(_handle, ESP_NETIF_REQUESTED_IP_ADDRESS, &range, sizeof(range));
        if (!r) {
            return r;
        }
    }
    if (lease_time) {
        auto r = set_dhcps_option(
            _handle, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &*lease_time, sizeof(*lease_time)
        );
        if (!r) {
            return r;
        }
    }
    if (cfg.offer_router) {
        dhcps_offer_t offer = *cfg.offer_router ? 1 : 0;
        auto r =
            set_dhcps_option(_handle, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &offer, sizeof(offer));
        if (!r) {
            return r;
        }
    }
    if (cfg.offer_dns) {
        dhcps_offer_t offer = *cfg.offer_dns ? 1 : 0;
        auto r = set_dhcps_option(_handle, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
        if (!r) {
            return r;
        }
    }
    if (uri) {
        // The driver stores the pointer itself, so the new URI is installed before the old one is
        // freed
        auto r = set_dhcps_option(
            _handle, ESP_NETIF_CAPTIVEPORTAL_URI, uri.get(), std::strlen(uri.get())
        );
        if (!r) {
            return r;
        }
        _captive_portal_uri = std::move(uri);
    }

    return try_dhcp_server_start();
}

result<void> interface::try_dhcp_server_stop() {
    if (!get_flags().contains(flag::dhcp_server)) {
        return error(idfxx::errc::invalid_state);
    }
    auto err = esp_netif_dhcps_stop(_handle);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

bool interface::is_dhcp_server_running() const {
    if (!get_flags().contains(flag::dhcp_server)) {
        return false;
    }
    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
    esp_netif_dhcps_get_status(_handle, &status);
    return status == ESP_NETIF_DHCP_STARTED;
}

#endif // CONFIG_LWIP_DHCPS

// =============================================================================
// Subsystem lifecycle
// =============================================================================

result<void> try_init() {
    auto err = esp_netif_init();
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

result<void> try_deinit() {
    auto err = esp_netif_deinit();
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

// =============================================================================
// Discovery
// =============================================================================

size_t get_nr_of_ifs() {
    return esp_netif_get_nr_of_ifs();
}

std::optional<interface> get_default() {
    auto* handle = esp_netif_get_default_netif();
    if (handle == nullptr) {
        return std::nullopt;
    }
    return interface::wrap(handle);
}

void set_default(interface& iface) {
    esp_netif_set_default_netif(iface.idf_handle());
}

std::optional<interface> find_by_key(const char* key) {
    auto* handle = esp_netif_get_handle_from_ifkey(key);
    if (handle == nullptr) {
        return std::nullopt;
    }
    return interface::wrap(handle);
}

// =============================================================================
// SNTP
// =============================================================================

namespace sntp {

result<void> try_init(const config& cfg) {
    esp_sntp_config_t native = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    native.smooth_sync = cfg.smooth_sync;
    native.server_from_dhcp = cfg.server_from_dhcp;
    native.wait_for_sync = cfg.wait_for_sync;
    native.start = cfg.start;
    native.renew_servers_after_new_IP = cfg.renew_servers_after_new_ip;
    native.ip_event_to_renew = static_cast<ip_event_t>(std::to_underlying(cfg.renew_event_id));
    native.num_of_servers =
        std::min(cfg.servers.size(), static_cast<size_t>(CONFIG_LWIP_SNTP_MAX_SERVERS));
    for (size_t i = 0; i < native.num_of_servers; ++i) {
        native.servers[i] = cfg.servers[i].c_str();
    }
    auto err = esp_netif_sntp_init(&native);
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

result<void> try_start() {
    auto err = esp_netif_sntp_start();
    if (err != ESP_OK) {
        return netif_error(err);
    }
    return {};
}

void deinit() {
    esp_netif_sntp_deinit();
}

bool detail::sync_wait_ms(std::chrono::milliseconds timeout) {
    auto ticks = idfxx::chrono::ticks(timeout);
    return esp_netif_sntp_sync_wait(ticks) == ESP_OK;
}

} // namespace sntp

} // namespace idfxx::netif
