// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "internal.hpp"
#include "ipv6_zone.hpp"

#include <idfxx/error>
#include <idfxx/event>
#include <idfxx/mdns>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <esp_log.h>
#include <esp_netif.h>
#include <mdns.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// The enumerators carry the C library's values, so they convert by cast.
static_assert(std::to_underlying(idfxx::mdns::record_type::a) == MDNS_TYPE_A);
static_assert(std::to_underlying(idfxx::mdns::record_type::ptr) == MDNS_TYPE_PTR);
static_assert(std::to_underlying(idfxx::mdns::record_type::txt) == MDNS_TYPE_TXT);
static_assert(std::to_underlying(idfxx::mdns::record_type::aaaa) == MDNS_TYPE_AAAA);
static_assert(std::to_underlying(idfxx::mdns::record_type::srv) == MDNS_TYPE_SRV);
static_assert(std::to_underlying(idfxx::mdns::record_type::any) == MDNS_TYPE_ANY);

static_assert(std::to_underlying(idfxx::mdns::transmission::unicast) == MDNS_QUERY_UNICAST);
static_assert(std::to_underlying(idfxx::mdns::transmission::multicast) == MDNS_QUERY_MULTICAST);

static_assert(std::to_underlying(idfxx::mdns::netif_action::enable_ipv4) == MDNS_EVENT_ENABLE_IP4);
static_assert(std::to_underlying(idfxx::mdns::netif_action::enable_ipv6) == MDNS_EVENT_ENABLE_IP6);
static_assert(
    std::to_underlying(idfxx::mdns::netif_action::announce_ipv4) == MDNS_EVENT_ANNOUNCE_IP4
);
static_assert(
    std::to_underlying(idfxx::mdns::netif_action::announce_ipv6) == MDNS_EVENT_ANNOUNCE_IP6
);
static_assert(
    std::to_underlying(idfxx::mdns::netif_action::disable_ipv4) == MDNS_EVENT_DISABLE_IP4
);
static_assert(
    std::to_underlying(idfxx::mdns::netif_action::disable_ipv6) == MDNS_EVENT_DISABLE_IP6
);
static_assert(
    std::to_underlying(idfxx::mdns::netif_action::ipv4_reverse_lookup) ==
    MDNS_EVENT_IP4_REVERSE_LOOKUP
);
static_assert(
    std::to_underlying(idfxx::mdns::netif_action::ipv6_reverse_lookup) ==
    MDNS_EVENT_IP6_REVERSE_LOOKUP
);

// hostname_info holds any hostname try_set_hostname() accepts.
static_assert(
    sizeof(idfxx::mdns::hostname_info::hostname) >= idfxx::mdns::internal::max_name_len + 1
);

namespace idfxx::mdns {

namespace internal {

std::atomic<bool> running{false};
std::atomic<uint32_t> generation{0};
std::mutex action_sema_mtx;

bool equal_ci(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) ==
            std::tolower(static_cast<unsigned char>(y));
    });
}

const char* protocol_label(enum protocol p) noexcept {
    return p == protocol::udp ? "_udp" : "_tcp";
}

std::optional<enum protocol> parse_protocol(const char* label) noexcept {
    if (label == nullptr) {
        return std::nullopt;
    }
    if (equal_ci(label, "_tcp")) {
        return protocol::tcp;
    }
    if (equal_ci(label, "_udp")) {
        return protocol::udp;
    }
    return std::nullopt;
}

namespace {

std::optional<std::string> opt_string(const char* s) {
    if (s == nullptr) {
        return std::nullopt;
    }
    return std::string(s);
}

} // namespace

answer to_answer(const mdns_result_t& r) {
    answer a;
    a.instance_name = opt_string(r.instance_name);
    a.service_type = opt_string(r.service_type);
    a.protocol = parse_protocol(r.proto);
    a.hostname = opt_string(r.hostname);
    if (r.port != 0) {
        a.port = r.port;
    }
    a.txt.reserve(r.txt_count);
    for (size_t i = 0; i < r.txt_count; ++i) {
        const mdns_txt_item_t& t = r.txt[i];
        txt_item item{.key = t.key != nullptr ? std::string(t.key) : std::string()};
        if (t.value != nullptr) {
            // Values may hold zero bytes, so use the reported length.
            size_t len = r.txt_value_len != nullptr ? r.txt_value_len[i] : std::strlen(t.value);
            item.value = std::string(t.value, len);
        }
        a.txt.push_back(std::move(item));
    }
    for (const mdns_ip_addr_t* ip = r.addr; ip != nullptr; ip = ip->next) {
        if (ip->addr.type == ESP_IPADDR_TYPE_V4) {
            a.ipv4_addresses.push_back(net::ipv4_addr(ip->addr.u_addr.ip4.addr));
        } else if (ip->addr.type == ESP_IPADDR_TYPE_V6) {
            const esp_ip6_addr_t& v6 = ip->addr.u_addr.ip6;
            const std::array<uint32_t, 4> words{v6.addr[0], v6.addr[1], v6.addr[2], v6.addr[3]};
            const bool arrived = r.esp_netif != nullptr;
            const uint8_t zone = answer_zone(
                words, v6.zone, arrived, arrived ? esp_netif_get_netif_impl_index(r.esp_netif) : 0
            );
            a.ipv6_addresses.push_back(net::ipv6_addr(words, zone));
        }
    }
    a.subtype = opt_string(r.subtype);
    if (r.esp_netif != nullptr) {
        a.netif_key = opt_string(esp_netif_get_ifkey(r.esp_netif));
    }
    switch (r.ip_protocol) {
    case MDNS_IP_PROTOCOL_V4:
        a.family = net::address_family::ipv4;
        break;
    case MDNS_IP_PROTOCOL_V6:
        a.family = net::address_family::ipv6;
        break;
    default:
        break;
    }
    a.ttl = std::chrono::seconds(r.ttl);
    return a;
}

std::vector<answer> to_answers(const mdns_result_t* r) {
    size_t n = 0;
    for (const mdns_result_t* p = r; p != nullptr; p = p->next) {
        ++n;
    }
    std::vector<answer> out;
    out.reserve(n);
    for (; r != nullptr; r = r->next) {
        out.push_back(to_answer(*r));
    }
    return out;
}

bool valid_txt_item(const txt_item& item) noexcept {
    if (item.key.empty() || item.key.find('=') != std::string::npos ||
        item.key.find('\0') != std::string::npos) {
        return false;
    }
    // One TXT string: the key, then "=" and the value if there is one.
    size_t len = item.key.size() + (item.value ? 1 + item.value->size() : 0);
    return len <= 255;
}

bool valid_txt(std::span<const txt_item> items) noexcept {
    return items.size() <= 255 && std::ranges::all_of(items, valid_txt_item);
}

std::vector<mdns_txt_item_with_value_len_t> to_c_txt(std::span<const txt_item> items) {
    std::vector<mdns_txt_item_with_value_len_t> out;
    out.reserve(items.size());
    for (const auto& item : items) {
        out.push_back({
            .key = item.key.c_str(),
            .value = item.value ? reinterpret_cast<const uint8_t*>(item.value->data()) : nullptr,
            .value_len = item.value ? static_cast<uint8_t>(item.value->size()) : uint8_t{0},
        });
    }
    return out;
}

bool valid_subtypes(std::span<const std::string> subtypes) noexcept {
    if (subtypes.size() > 255 ||
        !std::ranges::all_of(subtypes, [](const std::string& s) { return valid_subtype(s); })) {
        return false;
    }
    // The C library adds the subtypes of a list one at a time, and stops at
    // a duplicate. The subtypes it added before the duplicate stay
    // (mdns_service_subtype_update_multiple_items_for_host()).
    for (size_t i = 0; i < subtypes.size(); ++i) {
        for (size_t j = i + 1; j < subtypes.size(); ++j) {
            if (same_subtype(subtypes[i], subtypes[j])) {
                return false;
            }
        }
    }
    return true;
}

std::vector<mdns_subtype_item_t> to_c_subtypes(std::span<const std::string> subtypes) {
    std::vector<mdns_subtype_item_t> out;
    out.reserve(subtypes.size());
    for (const auto& s : subtypes) {
        out.push_back({.subtype = s.c_str()});
    }
    return out;
}

std::vector<mdns_ip_addr_t>
to_c_addresses(std::span<const net::ipv4_addr> ipv4, std::span<const net::ipv6_addr> ipv6) {
    std::vector<mdns_ip_addr_t> out(ipv4.size() + ipv6.size());
    size_t i = 0;
    for (const auto& a : ipv4) {
        out[i].addr.type = ESP_IPADDR_TYPE_V4;
        out[i].addr.u_addr.ip4.addr = a.addr();
        ++i;
    }
    for (const auto& a : ipv6) {
        out[i].addr.type = ESP_IPADDR_TYPE_V6;
        std::ranges::copy(a.addr(), out[i].addr.u_addr.ip6.addr);
        out[i].addr.u_addr.ip6.zone = a.zone();
        ++i;
    }
    for (size_t j = 0; j < out.size(); ++j) {
        out[j].next = j + 1 < out.size() ? &out[j + 1] : nullptr;
    }
    return out;
}

} // namespace internal

namespace {

using internal::TAG;
using internal::wrap_limited;

// Serializes init() and deinit().
std::mutex lifecycle_mtx;

// The interfaces that try_register_netif() registered, so that
// try_unregister_netif() never disables a predefined interface. netif_mtx
// guards the list and serializes try_register_netif() and
// try_unregister_netif(). Nothing on the mDNS task takes it.
std::mutex netif_mtx;
std::vector<esp_netif_t*> registered_netifs;

void netifs_clear() {
    std::lock_guard lk(netif_mtx);
    registered_netifs.clear();
}

// Empties the registries of services, delegated hosts, browsers and netifs.
void clear_registries() {
#ifdef CONFIG_MDNS_ENABLE_BROWSE
    internal::browsers_clear();
#endif
    internal::services_clear();
    internal::hosts_clear();
    netifs_clear();
}

// Runs on the mDNS task while it processes an action, so it must not call
// mDNS functions, and posts without waiting so it never blocks that task.
void on_hostname_changed(const char* hostname, void* /*arg*/) {
    hostname_info info{};
    if (hostname != nullptr) {
        std::strncpy(info.hostname, hostname, sizeof(info.hostname) - 1);
    }
    auto r = event_loop::system().try_post(hostname_changed, info, std::chrono::milliseconds(0));
    if (!r) {
        ESP_LOGW(TAG, "Dropped hostname_changed event: %s", r.error().message().c_str());
    }
}

} // namespace

// =============================================================================
// Errors
// =============================================================================

const error_category& mdns_category() noexcept {
    static const error_category instance{};
    return instance;
}

const char* error_category::name() const noexcept {
    return "mdns::Error";
}

std::string error_category::message(int ec) const {
    switch (errc(ec)) {
    case errc::limit_reached:
        return "mDNS capacity limit reached";
    default:
        return "Unknown mDNS error";
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void init() {
    unwrap(try_init());
}
#endif

result<void> try_init() {
    std::lock_guard lk(lifecycle_mtx);
    if (internal::running.load()) {
        return {};
    }
    if (auto r = internal::queries_init(); !r) {
        return r;
    }
    if (auto r = wrap(mdns_init()); !r) {
        return r;
    }
    // Cleared by mdns_free(), so it is registered on every init().
    if (esp_err_t err = mdns_register_hostname_changed_callback(on_hostname_changed, nullptr);
        err != ESP_OK) {
        mdns_free();
        return wrap(err);
    }
    // Already empty unless a deinit() raced an object's creation.
    clear_registries();
    internal::running.store(true);
    return {};
}

void deinit() {
    std::lock_guard lk(lifecycle_mtx);
    if (!internal::running.exchange(false)) {
        return;
    }
    internal::generation.fetch_add(1);
    // Queries first: their searches must be deleted while the C library's lock
    // still exists, which mdns_free() destroys.
    internal::queries_shutdown();
    clear_registries();
    mdns_free();
}

// =============================================================================
// Hostname and default instance name
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void set_hostname(std::string_view hostname) {
    unwrap(try_set_hostname(hostname));
}

std::string get_hostname() {
    return unwrap(try_get_hostname());
}

void set_instance_name(std::string_view instance_name) {
    unwrap(try_set_instance_name(instance_name));
}
#endif

result<void> try_set_hostname(std::string_view hostname) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_name(hostname)) {
        return error(idfxx::errc::invalid_arg);
    }
    std::string name(hostname);
    // Held across the check, so that delegated_host::make() cannot take the
    // name in between.
    std::lock_guard lk(internal::action_sema_mtx);
    if (internal::is_delegated_hostname(name)) {
        return error(idfxx::errc::invalid_state);
    }
    return wrap_limited(mdns_hostname_set(name.c_str()));
}

result<std::string> try_get_hostname() {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    char buf[MDNS_NAME_BUF_LEN] = {};
    if (auto r = wrap(mdns_hostname_get(buf)); !r) {
        return error(r.error());
    }
    return std::string(buf);
}

result<void> try_set_instance_name(std::string_view instance_name) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_name(instance_name)) {
        return error(idfxx::errc::invalid_arg);
    }
    std::string name(instance_name);
    esp_err_t err = mdns_instance_name_set(name.c_str());
    if (err == ESP_ERR_INVALID_ARG) {
        // The name itself is valid, so the C library is missing a hostname.
        return error(idfxx::errc::invalid_state);
    }
    return wrap_limited(err);
}

bool hostname_exists(std::string_view hostname) {
    if (!internal::running.load() || !internal::valid_name(hostname)) {
        return false;
    }
    return mdns_hostname_exists(std::string(hostname).c_str());
}

// =============================================================================
// Network interfaces
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
void register_netif(netif::interface& iface) {
    unwrap(try_register_netif(iface));
}

void unregister_netif(netif::interface& iface) {
    unwrap(try_unregister_netif(iface));
}

void post_netif_action(netif::interface& iface, flags<netif_action> actions) {
    unwrap(try_post_netif_action(iface, actions));
}
#endif

result<void> try_register_netif(netif::interface& iface) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    esp_netif_t* handle = iface.idf_handle();
    std::lock_guard lk(netif_mtx);
    // Reserved first, so that recording the interface cannot fail after the
    // C library has registered it.
    registered_netifs.reserve(registered_netifs.size() + 1);
    // ESP_ERR_NO_MEM, so limit_reached, when every interface slot is taken.
    if (auto r = wrap_limited(mdns_register_netif(handle)); !r) {
        return r;
    }
    if (std::ranges::find(registered_netifs, handle) == registered_netifs.end()) {
        registered_netifs.push_back(handle);
    }
    return {};
}

// mdns_unregister_netif() only clears the interface's slot. It leaves the
// slot's PCB ready, so the next interface registered into the slot would
// never join the multicast group. The C library's disable action clears the
// slot and resets its PCB (mdns_priv_pcb_disable(), mdns_pcb.c), so this posts
// that action instead, and waits until the mDNS task has run it.
result<void> try_unregister_netif(netif::interface& iface) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    esp_netif_t* handle = iface.idf_handle();
    std::lock_guard lk(netif_mtx);
    auto it = std::ranges::find(registered_netifs, handle);
    if (it == registered_netifs.end()) {
        // Never disables a predefined interface.
        return error(idfxx::errc::not_found);
    }
    // ESP_ERR_INVALID_STATE once the interface has no slot. ESP_ERR_NO_MEM
    // here is an allocation failure: a full queue drops the action silently.
    esp_err_t err = mdns_netif_action(
        handle, static_cast<mdns_event_actions_t>(MDNS_EVENT_DISABLE_IP4 | MDNS_EVENT_DISABLE_IP6)
    );
    if (err == ESP_ERR_INVALID_STATE) {
        // A disable action that the caller posted has unregistered it.
        registered_netifs.erase(it);
        return error(idfxx::errc::not_found);
    }
    if (auto r = wrap(err); !r) {
        return r;
    }
    // A no-op action finds the interface until the mDNS task has run the
    // disable action. Each poll queues a no-op action for the task, so the
    // polls are 10 ms apart to leave room in the queue for other actions.
    constexpr TickType_t poll_ticks = pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1;
    for (int polls = 0; polls < 100; ++polls) {
        vTaskDelay(poll_ticks);
        err = mdns_netif_action(handle, static_cast<mdns_event_actions_t>(0));
        if (err == ESP_ERR_INVALID_STATE) {
            registered_netifs.erase(it);
            return {};
        }
        if (auto r = wrap(err); !r) {
            return r;
        }
    }
    // The disable action was most likely dropped from a full queue. The
    // interface stays registered, so a later call can try again.
    return error(errc::limit_reached);
}

result<void> try_post_netif_action(netif::interface& iface, flags<netif_action> actions) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    // A full action queue is not reported (the action is dropped), so
    // ESP_ERR_NO_MEM here is an allocation failure.
    return wrap(mdns_netif_action(
        iface.idf_handle(), static_cast<mdns_event_actions_t>(idfxx::to_underlying(actions))
    ));
}

} // namespace idfxx::mdns

// =============================================================================
// String conversion
// =============================================================================

namespace idfxx {

std::string to_string(mdns::protocol p) {
    switch (p) {
    case mdns::protocol::tcp:
    case mdns::protocol::udp:
        return mdns::internal::protocol_label(p);
    default:
        return "unknown(" + std::to_string(std::to_underlying(p)) + ")";
    }
}

std::string to_string(mdns::record_type t) {
    switch (t) {
    case mdns::record_type::a:
        return "A";
    case mdns::record_type::ptr:
        return "PTR";
    case mdns::record_type::txt:
        return "TXT";
    case mdns::record_type::aaaa:
        return "AAAA";
    case mdns::record_type::srv:
        return "SRV";
    case mdns::record_type::any:
        return "ANY";
    default:
        return "unknown(" + std::to_string(std::to_underlying(t)) + ")";
    }
}

} // namespace idfxx
