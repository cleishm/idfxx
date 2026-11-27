// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "internal.hpp"

#include <idfxx/error>
#include <idfxx/mdns>

#include <algorithm>
#include <cstdint>
#include <esp_log.h>
#include <mdns.h>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace idfxx::mdns {

namespace {

using internal::TAG;
using internal::wrap_limited;

// Names of the delegated hosts this wrapper added. The C library silently
// ignores adding a name it already answers for, so duplicates are caught
// here. Removal completes asynchronously on the mDNS task, so asking the C
// library instead would reject re-adding a name immediately after its host was
// destroyed. The queued removal is processed before the queued add.
//
// Lock order: internal::action_sema_mtx -> hosts_mtx. It is never held with
// services_mtx, and nothing on the mDNS task takes it.
std::mutex hosts_mtx;
std::vector<std::string> host_names;

// Whether a delegated host has this name. The caller holds hosts_mtx.
bool host_name_taken(std::string_view name) {
    return std::ranges::any_of(host_names, [&](const std::string& n) {
        return internal::equal_ci(n, name);
    });
}

bool addresses_supported(
    std::span<const net::ipv4_addr> ipv4,
    std::span<const net::ipv6_addr> ipv6
) noexcept {
#ifndef CONFIG_LWIP_IPV4
    if (!ipv4.empty()) {
        return false;
    }
#endif
#ifndef CONFIG_LWIP_IPV6
    if (!ipv6.empty()) {
        return false;
    }
#endif
    (void)ipv4;
    (void)ipv6;
    return true;
}

// The device's hostname, if one is set.
std::optional<std::string> own_hostname() {
    char buf[MDNS_NAME_BUF_LEN] = {};
    if (mdns_hostname_get(buf) != ESP_OK) {
        return std::nullopt;
    }
    return std::string(buf);
}

// Services and the registry of live ones
// ======================================
//
// The C library finds a service by instance name, type, protocol and host,
// and treats a NULL instance name as "any instance". It also renames an
// instance by itself when another responder on the network claims the name
// (mdns_receive.c), without telling anyone. So the wrapper keeps a registry
// of the live services it created, keyed by type (without regard to case),
// protocol and host, and enforces one rule:
//
//   A service without an instance name must be the only service of its type
//   and protocol on its host.
//
// So whenever a key has more than one service, every one of them is named.
// After creation, a C call for a service that is the only one of its key
// passes a NULL instance name, which matches exactly that service and keeps
// working whatever the instance has been renamed to (and follows the default
// instance name for an unnamed service). A service sharing its key with
// others passes its explicit instance name. If the network renames that
// instance, the calls no longer find it (a documented limitation).
//
// The calls that can remove a subtype (try_remove_subtype() and
// try_set_subtypes()) are the exception. The C library writes the instance
// name passed to them into the goodbye packets for a removed subtype
// (mdns_priv_send_bye_subtype(), mdns_send.c), and crashes on NULL when an
// interface is up. So they always pass the explicit instance name, and a
// service without one may not remove a subtype. A named service that the
// network has renamed is then not found by these two calls, even when it is
// the only one of its key (a documented limitation). To tell whether
// try_set_subtypes() removes a subtype, each registry entry keeps the
// subtypes that the C library holds for the service.
//
// A renamed service that shares its key can't be removed either: the removal
// fails with ESP_ERR_NOT_FOUND, and the C library keeps advertising the
// service. Its registry entry then moves to a list of stranded services
// (`stranded`), which keeps a copy of its identity. A stranded service still
// counts for its key in instance_arg(), so a service that shares the key with
// it keeps passing its explicit instance name. (A NULL name could match the
// stranded service in the C library.) For the same reason, a stranded service
// blocks the creation of a service without an instance name of its key. It
// does not block a named service by name, because its real instance name is
// unknown. Stranded entries are dropped by services_clear() (deinit() and
// try_init()), and with their delegated host when it is destroyed, because
// the C library removes the host's services with it. A removal that fails
// because the delegated host is gone does not strand the service.
//
// The host of a key is std::nullopt for the device itself rather than its
// hostname, which conflict resolution can change.
//
// services_mtx guards the registry and the stranded list. It is held across
// every C call made for a service (the add, the mutators and the removal), so
// the count used to pick the instance name cannot change before the call
// runs. Lock order: services_mtx -> the C library's service lock. Those calls
// run synchronously under the service lock and never wait for the mDNS task.
// The only waits in them are lwIP tcpip_api_call()s to send packets, and the
// tcpip thread never takes the service lock. Nothing on the mDNS task (the
// query and browse notifiers, the hostname-changed callback) or on the
// esp_timer task takes services_mtx, and it is never held across a sleep, a
// queue-full retry or internal::action_sema_mtx.
struct service_id {
    // Explicit instance name. nullopt uses the default instance name.
    std::optional<std::string> instance;
    std::string type;
    enum protocol protocol;
    // The delegated host's name, or nullopt for the device itself.
    std::optional<std::string> host;
    // The subtypes that the C library holds for the service, compared as it
    // compares them (internal::same_subtype()). Changed only together with a
    // C call that succeeds, so that it always matches the C library's list.
    std::vector<std::string> subtypes;

    [[nodiscard]] bool same_key(const service_id& o) const noexcept {
        return protocol == o.protocol && internal::equal_ci(type, o.type) &&
            host.has_value() == o.host.has_value() && (!host || internal::equal_ci(*host, *o.host));
    }
};

std::mutex services_mtx;
std::vector<service_id*> services;
// Services that the C library still advertises after their removal failed
// (see above).
std::vector<service_id> stranded;

// Called with services_mtx held.
bool registered(const service_id* id) noexcept {
    return std::ranges::find(services, id) != services.end();
}

// Whether a stranded service has the key of `id`. Called with services_mtx
// held.
bool stranded_key(const service_id& id) noexcept {
    return std::ranges::any_of(stranded, [&](const service_id& e) { return e.same_key(id); });
}

// The instance name to pass to the C library for a registered service.
// Called with services_mtx held.
const char* instance_arg(const service_id& id) noexcept {
    auto count =
        std::ranges::count_if(services, [&](const service_id* e) { return e->same_key(id); }) +
        std::ranges::count_if(stranded, [&](const service_id& e) { return e.same_key(id); });
    if (count <= 1) {
        return nullptr; // the only one: "any instance" is this service
    }
    // Every service sharing a key has an instance name.
    return id.instance ? id.instance->c_str() : nullptr;
}

// Whether replacing the subtypes `current` with `next` removes one of them.
bool drops_subtype(
    std::span<const std::string> current,
    std::span<const std::string> next
) noexcept {
    return std::ranges::any_of(current, [&](const std::string& c) {
        return std::ranges::none_of(next, [&](const std::string& n) {
            return internal::same_subtype(c, n);
        });
    });
}

} // namespace

void internal::hosts_clear() {
    std::lock_guard lk(hosts_mtx);
    host_names.clear();
}

void internal::services_clear() {
    std::lock_guard lk(services_mtx);
    services.clear();
    stranded.clear();
}

bool internal::is_delegated_hostname(std::string_view name) {
    std::lock_guard lk(hosts_mtx);
    return host_name_taken(name);
}

// =============================================================================
// delegated_host
// =============================================================================

struct delegated_host::state {
    std::string name;
    uint32_t generation;

    // The service is running in the session this host was added in.
    [[nodiscard]] bool live() const noexcept { return internal::live(generation); }
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
delegated_host::delegated_host(config cfg)
    : delegated_host(unwrap(make(std::move(cfg)))) {}
#endif

delegated_host::delegated_host(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<delegated_host> delegated_host::make(config cfg) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_name(*cfg.name) ||
        (cfg.ipv4_addresses.empty() && cfg.ipv6_addresses.empty())) {
        return error(idfxx::errc::invalid_arg);
    }
    if (!addresses_supported(cfg.ipv4_addresses, cfg.ipv6_addresses)) {
        return error(idfxx::errc::not_supported);
    }
    auto addrs = internal::to_c_addresses(cfg.ipv4_addresses, cfg.ipv6_addresses);
    // Held across the hostname check and the add, so that try_set_hostname()
    // cannot take the name in between.
    std::lock_guard sema_lk(internal::action_sema_mtx);
    if (auto own = own_hostname(); own && internal::equal_ci(*own, *cfg.name)) {
        return error(idfxx::errc::invalid_state);
    }
    {
        std::lock_guard lk(hosts_mtx);
        if (host_name_taken(*cfg.name)) {
            return error(idfxx::errc::invalid_state);
        }
        host_names.push_back(*cfg.name);
    }
    // Waits until the mDNS task has added the host, after any removal queued
    // before it, so services added on the host from now on are not removed
    // with an earlier host of the same name.
    if (auto r = wrap_limited(mdns_delegate_hostname_add(cfg.name->c_str(), addrs.data())); !r) {
        std::lock_guard lk(hosts_mtx);
        std::erase(host_names, *cfg.name);
        return error(r.error());
    }
    return delegated_host(
        std::make_unique<state>(
            state{.name = std::move(*cfg.name), .generation = internal::generation.load()}
        )
    );
}

void delegated_host::_release() noexcept {
    if (!_state) {
        return;
    }
    if (_state->live()) {
        // The C library removes the host's services with it, stranded ones
        // included. Services that outlive their host (against the contract)
        // are no longer registered, so they make no further C calls.
        {
            std::lock_guard lk(services_mtx);
            auto on_host = [&](const service_id& e) {
                return e.host && internal::equal_ci(*e.host, _state->name);
            };
            std::erase_if(services, [&](const service_id* e) { return on_host(*e); });
            std::erase_if(stranded, on_host);
        }
        // Queued for the mDNS task, so it fails while the queue is full.
        esp_err_t err = internal::retry_while_queue_full([&] {
            return mdns_delegate_hostname_remove(_state->name.c_str());
        });
        if (err == ESP_OK) {
            std::lock_guard lk(hosts_mtx);
            std::erase(host_names, _state->name);
        } else {
            // The C library still answers for the name, so it stays taken: a
            // later make() with it fails with invalid_state rather than
            // silently keeping the old addresses.
            ESP_LOGE(
                TAG,
                "Could not remove delegated host %s: %s",
                _state->name.c_str(),
                esp_err_to_name(err)
            );
        }
    }
    _state.reset();
}

delegated_host::~delegated_host() {
    _release();
}

delegated_host::delegated_host(delegated_host&& other) noexcept = default;

delegated_host& delegated_host::operator=(delegated_host&& other) noexcept {
    if (this != &other) {
        _release();
        _state = std::move(other._state);
    }
    return *this;
}

const std::string& delegated_host::name() const noexcept {
    return _state->name;
}

result<void> delegated_host::try_set_addresses(
    std::span<const net::ipv4_addr> ipv4_addresses,
    std::span<const net::ipv6_addr> ipv6_addresses
) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (ipv4_addresses.empty() && ipv6_addresses.empty()) {
        return error(idfxx::errc::invalid_arg);
    }
    if (!addresses_supported(ipv4_addresses, ipv6_addresses)) {
        return error(idfxx::errc::not_supported);
    }
    auto addrs = internal::to_c_addresses(ipv4_addresses, ipv6_addresses);
    return wrap_limited(mdns_delegate_hostname_set_address(_state->name.c_str(), addrs.data()));
}

// =============================================================================
// service
// =============================================================================

struct service::state {
    // Registered in `services` while the service exists (see above).
    service_id id;
    net::port_number port;
    uint32_t generation;

    [[nodiscard]] const char* type_c() const noexcept { return id.type.c_str(); }
    [[nodiscard]] const char* proto_c() const noexcept {
        return internal::protocol_label(id.protocol);
    }
    [[nodiscard]] const char* host_c() const noexcept {
        return id.host ? id.host->c_str() : nullptr;
    }

    // The service is running in the session this service was created in.
    [[nodiscard]] bool live() const noexcept { return internal::live(generation); }

    // The host to name to the subtype calls. Unlike the other calls, they do
    // not resolve NULL to the device's hostname but treat it as any host, so
    // the device's own services name it, read at call time as conflict
    // resolution can change it.
    [[nodiscard]] std::optional<std::string> subtype_host() const {
        return id.host ? id.host : own_hostname();
    }

    // Makes a C call that takes this service's instance name, under
    // services_mtx. A service no longer registered (its delegated host was
    // destroyed first) is not found.
    template<typename F>
    [[nodiscard]] esp_err_t call(F&& f) {
        std::lock_guard lk(services_mtx);
        if (!registered(&id)) {
            return ESP_ERR_NOT_FOUND;
        }
        return f(instance_arg(id));
    }
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
service::service(config cfg)
    : service(unwrap(make(std::move(cfg)))) {}

service::service(delegated_host& host, config cfg)
    : service(unwrap(make(host, std::move(cfg)))) {}
#endif

service::service(std::unique_ptr<state> s) noexcept
    : _state(std::move(s)) {}

result<service> service::make(config cfg) {
    return _make(nullptr, std::move(cfg));
}

result<service> service::make(delegated_host& host, config cfg) {
    return _make(&host, std::move(cfg));
}

result<service> service::_make(const delegated_host* host, config cfg) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_name(*cfg.type) || *cfg.port == 0 ||
        (cfg.instance_name && !internal::valid_name(*cfg.instance_name)) ||
        !internal::valid_txt(cfg.txt) || !internal::valid_subtypes(cfg.subtypes)) {
        return error(idfxx::errc::invalid_arg);
    }
    // The C library refuses every service add while the device has no
    // hostname, also one for a delegated host.
    if ((host != nullptr && (!host->_state || !host->_state->live())) || !own_hostname()) {
        return error(idfxx::errc::invalid_state);
    }

    auto s = std::make_unique<state>(state{
        .id =
            service_id{
                .instance = std::move(cfg.instance_name),
                .type = std::move(*cfg.type),
                .protocol = *cfg.protocol,
                .host = host != nullptr ? std::optional<std::string>(host->name()) : std::nullopt,
                // Set by try_set_subtypes() below.
                .subtypes = {},
            },
        .port = *cfg.port,
        .generation = internal::generation.load(),
    });

    auto txt = internal::to_c_txt(cfg.txt);
    {
        std::lock_guard lk(services_mtx);
        // See "Services and the registry of live ones" above.
        for (const service_id* e : services) {
            if (e->same_key(s->id) &&
                (!s->id.instance || !e->instance ||
                 internal::equal_ci(*e->instance, *s->id.instance))) {
                return error(idfxx::errc::invalid_state);
            }
        }
        if (!s->id.instance && stranded_key(s->id)) {
            return error(idfxx::errc::invalid_state);
        }
        // NULL for no instance name, so the service follows the default
        // instance name.
        esp_err_t err = mdns_service_add_for_host_with_explicit_txt_item_value_len(
            s->id.instance ? s->id.instance->c_str() : nullptr,
            s->type_c(),
            s->proto_c(),
            s->host_c(),
            s->port,
            txt.data(),
            txt.size()
        );
        if (err == ESP_ERR_INVALID_ARG) {
            // The arguments are valid and a hostname is set, so the service
            // exists (a stranded service or one this wrapper did not create).
            return error(idfxx::errc::invalid_state);
        }
        // ESP_ERR_NO_MEM, so limit_reached, when CONFIG_MDNS_MAX_SERVICES
        // services are already advertised.
        if (auto r = wrap_limited(err); !r) {
            return error(r.error());
        }
        services.push_back(&s->id);
    }

    service svc(std::move(s));
    if (!cfg.subtypes.empty()) {
        // A deinit() since the service was added gives invalid_state, as
        // try_set_subtypes() does.
        if (!svc._state->live()) {
            return error(idfxx::errc::invalid_state);
        }
        if (auto r = svc._set_subtypes(std::move(cfg.subtypes)); !r) {
            return error(r.error());
        }
    }
    return svc;
}

void service::_release() noexcept {
    if (!_state) {
        return;
    }
    // Checked before services_mtx is taken, because hosts_mtx and
    // services_mtx are never held together, so the host can be destroyed
    // after the check. This race is acceptable. If the host's destructor takes
    // services_mtx first, it unregisters this service, and no removal is made.
    // If the removal runs first, the C library still has the host, and the
    // destructor then drops a stranded entry with the host's other ones.
    const bool host_exists = !_state->id.host || internal::is_delegated_hostname(*_state->id.host);
    {
        std::lock_guard lk(services_mtx);
        auto it = std::ranges::find(services, &_state->id);
        if (it != services.end()) {
            if (_state->live()) {
                // Runs synchronously under the C library's lock, so a full
                // action queue cannot make it fail.
                esp_err_t err = mdns_service_remove_for_host(
                    instance_arg(_state->id), _state->type_c(), _state->proto_c(), _state->host_c()
                );
                // Not found while its host exists: the network has renamed
                // the service, and the C library still advertises it. See
                // "Services and the registry of live ones" above.
                if (err == ESP_ERR_NOT_FOUND && host_exists) {
                    stranded.push_back(std::move(_state->id));
                }
            }
            services.erase(it);
        }
    }
    _state.reset();
}

service::~service() {
    _release();
}

service::service(service&& other) noexcept = default;

service& service::operator=(service&& other) noexcept {
    if (this != &other) {
        _release();
        _state = std::move(other._state);
    }
    return *this;
}

const std::string& service::type() const noexcept {
    return _state->id.type;
}

enum protocol service::protocol() const noexcept {
    return _state->id.protocol;
}

const std::optional<std::string>& service::instance_name() const noexcept {
    return _state->id.instance;
}

net::port_number service::port() const noexcept {
    return _state->port;
}

result<void> service::try_set_port(net::port_number port) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (port == 0) {
        return error(idfxx::errc::invalid_arg);
    }
    if (auto r = wrap(_state->call([&](const char* instance) {
            return mdns_service_port_set_for_host(
                instance, _state->type_c(), _state->proto_c(), _state->host_c(), port
            );
        }));
        !r) {
        return r;
    }
    _state->port = port;
    return {};
}

result<void> service::try_set_instance_name(std::string_view instance_name) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_name(instance_name)) {
        return error(idfxx::errc::invalid_arg);
    }
    std::string name(instance_name);
    return wrap(_state->call([&](const char* instance) {
        // The C library does not check the new name against the other
        // services of the key, so a clash would leave two services that the
        // wrapper can't tell apart.
        service_id& self = _state->id;
        if (std::ranges::any_of(services, [&](const service_id* e) {
                return e != &self && e->same_key(self) && e->instance &&
                    internal::equal_ci(*e->instance, name);
            })) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t err = mdns_service_instance_name_set_for_host(
            instance, _state->type_c(), _state->proto_c(), _state->host_c(), name.c_str()
        );
        if (err == ESP_OK) {
            self.instance = std::move(name);
        }
        return err;
    }));
}

result<void> service::try_set_txt(std::span<const txt_item> items) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_txt(items)) {
        return error(idfxx::errc::invalid_arg);
    }
    auto txt = internal::to_c_txt(items);
    return wrap(_state->call([&](const char* instance) {
        return mdns_service_txt_set_for_host_with_explicit_txt_item_value_len(
            instance,
            _state->type_c(),
            _state->proto_c(),
            _state->host_c(),
            txt.data(),
            static_cast<uint8_t>(txt.size())
        );
    }));
}

result<void> service::try_set_txt_item(const txt_item& item) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_txt_item(item)) {
        return error(idfxx::errc::invalid_arg);
    }
    return wrap(_state->call([&](const char* instance) {
        return mdns_service_txt_item_set_for_host_with_explicit_value_len(
            instance,
            _state->type_c(),
            _state->proto_c(),
            _state->host_c(),
            item.key.c_str(),
            item.value ? item.value->data() : nullptr,
            item.value ? static_cast<uint8_t>(item.value->size()) : uint8_t{0}
        );
    }));
}

result<void> service::try_remove_txt_item(std::string_view key) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    // The C library takes the key as a NUL-terminated string.
    if (key.empty() || key.find('\0') != std::string_view::npos) {
        return error(idfxx::errc::invalid_arg);
    }
    std::string key_s(key);
    return wrap(_state->call([&](const char* instance) {
        return mdns_service_txt_item_remove_for_host(
            instance, _state->type_c(), _state->proto_c(), _state->host_c(), key_s.c_str()
        );
    }));
}

result<void> service::try_add_subtype(std::string_view subtype) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_subtype(subtype)) {
        return error(idfxx::errc::invalid_arg);
    }
    std::string subtype_s(subtype);
    return wrap(_state->call([&](const char* instance) {
        auto host = _state->subtype_host();
        if (!host) {
            return ESP_ERR_INVALID_STATE;
        }
        // Reserved first, so that recording the subtype cannot fail after
        // the C library has added it.
        std::vector<std::string>& held = _state->id.subtypes;
        held.reserve(held.size() + 1);
        esp_err_t err = mdns_service_subtype_add_for_host(
            instance, _state->type_c(), _state->proto_c(), host->c_str(), subtype_s.c_str()
        );
        if (err == ESP_OK) {
            held.push_back(std::move(subtype_s));
        }
        return err;
    }));
}

// The subtype calls below do not use state::call(): they always pass the
// service's explicit instance name (see "Services and the registry of live
// ones" above).

result<void> service::try_remove_subtype(std::string_view subtype) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_subtype(subtype)) {
        return error(idfxx::errc::invalid_arg);
    }
    std::string subtype_s(subtype);
    std::lock_guard lk(services_mtx);
    service_id& self = _state->id;
    if (!self.instance) {
        return error(idfxx::errc::not_supported);
    }
    if (!registered(&self)) {
        return error(idfxx::errc::not_found);
    }
    auto host = _state->subtype_host();
    if (!host) {
        return error(idfxx::errc::invalid_state);
    }
    esp_err_t err = mdns_service_subtype_remove_for_host(
        self.instance->c_str(),
        _state->type_c(),
        _state->proto_c(),
        host->c_str(),
        subtype_s.c_str()
    );
    // ESP_ERR_NO_MEM comes after the C library has removed the subtype, when
    // it can't build the goodbye packets.
    if (err == ESP_OK || err == ESP_ERR_NO_MEM) {
        std::erase_if(self.subtypes, [&](const std::string& s) {
            return internal::same_subtype(s, subtype_s);
        });
    }
    return wrap(err);
}

result<void> service::try_set_subtypes(std::span<const std::string> subtypes) {
    if (!_state || !_state->live()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_subtypes(subtypes)) {
        return error(idfxx::errc::invalid_arg);
    }
    // Copied first, so that recording the new list cannot fail after the C
    // library has applied it.
    std::vector<std::string> next(subtypes.begin(), subtypes.end());
    return _set_subtypes(std::move(next));
}

result<void> service::_set_subtypes(std::vector<std::string> next) {
    // The C views point into the strings of `next`. Moving the vector into
    // the service keeps its strings in place, so they stay valid.
    auto items = internal::to_c_subtypes(next);
    std::lock_guard lk(services_mtx);
    service_id& self = _state->id;
    if (!self.instance && drops_subtype(self.subtypes, next)) {
        return error(idfxx::errc::not_supported);
    }
    if (!registered(&self)) {
        return error(idfxx::errc::not_found);
    }
    auto host = _state->subtype_host();
    if (!host) {
        return error(idfxx::errc::invalid_state);
    }
    // NULL only for a service without an instance name, which is the only
    // one of its key and keeps all of its subtypes here, so the C library
    // sends no goodbye packets.
    esp_err_t err = mdns_service_subtype_update_multiple_items_for_host(
        self.instance ? self.instance->c_str() : nullptr,
        _state->type_c(),
        _state->proto_c(),
        host->c_str(),
        items.data(),
        static_cast<uint8_t>(items.size())
    );
    if (err == ESP_OK) {
        self.subtypes = std::move(next);
    } else if (err == ESP_ERR_NO_MEM) {
        // The C library has freed the old list and removes the subtypes it
        // added before the failure, so the service has none.
        self.subtypes.clear();
    }
    return wrap(err);
}

// =============================================================================
// service_exists
// =============================================================================

namespace {

// An empty hostname would match services on any host, so the host is always
// named.
bool service_exists_on(
    const std::string& hostname,
    std::string_view type,
    enum protocol protocol,
    std::optional<std::string_view> instance_name
) {
    std::string type_s(type);
    if (instance_name) {
        return mdns_service_exists_with_instance(
            std::string(*instance_name).c_str(),
            type_s.c_str(),
            internal::protocol_label(protocol),
            hostname.c_str()
        );
    }
    return mdns_service_exists(
        type_s.c_str(), internal::protocol_label(protocol), hostname.c_str()
    );
}

bool valid_exists_query(
    std::string_view type,
    std::optional<std::string_view> instance_name
) noexcept {
    return internal::running.load() && internal::valid_name(type) &&
        (!instance_name || internal::valid_name(*instance_name));
}

} // namespace

bool service_exists(
    std::string_view type,
    enum protocol protocol,
    std::optional<std::string_view> instance_name
) {
    if (!valid_exists_query(type, instance_name)) {
        return false;
    }
    auto hostname = own_hostname();
    return hostname && service_exists_on(*hostname, type, protocol, instance_name);
}

bool service_exists(
    const delegated_host& host,
    std::string_view type,
    enum protocol protocol,
    std::optional<std::string_view> instance_name
) {
    return valid_exists_query(type, instance_name) &&
        service_exists_on(host.name(), type, protocol, instance_name);
}

// =============================================================================
// Local service lookups
// =============================================================================

namespace {

result<std::vector<answer>> lookup(
    bool self_hosted,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options
) {
    if (!internal::running.load()) {
        return error(idfxx::errc::invalid_state);
    }
    if (!internal::valid_name(type) ||
        (options.instance_name && !internal::valid_name(*options.instance_name)) ||
        (options.subtype && !internal::valid_subtype(*options.subtype)) ||
        (options.max_results && *options.max_results == 0)) {
        return error(idfxx::errc::invalid_arg);
    }
    std::string type_s(type);
    const char* instance = options.instance_name ? options.instance_name->c_str() : nullptr;
    const char* proto = internal::protocol_label(protocol);
    // A limit of zero would stop after the first match, so ask for all of them.
    size_t max_results = options.max_results ? *options.max_results : SIZE_MAX;

    mdns_result_t* raw = nullptr;
    esp_err_t err;
    if (options.subtype) {
        err = self_hosted
            ? mdns_lookup_selfhosted_service_with_subtype(
                  instance, type_s.c_str(), proto, options.subtype->c_str(), max_results, &raw
              )
            : mdns_lookup_delegated_service_with_subtype(
                  instance, type_s.c_str(), proto, options.subtype->c_str(), max_results, &raw
              );
    } else {
        err = self_hosted
            ? mdns_lookup_selfhosted_service(instance, type_s.c_str(), proto, max_results, &raw)
            : mdns_lookup_delegated_service(instance, type_s.c_str(), proto, max_results, &raw);
    }
    internal::results_ptr results(raw);
    // The lookups only fail for invalid arguments. A failed allocation
    // returns no results.
    if (auto r = wrap(err); !r) {
        return error(r.error());
    }
    return internal::to_answers(results.get());
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
std::vector<answer> lookup_services(
    self_hosted_t tag,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options
) {
    return unwrap(try_lookup_services(tag, type, protocol, options));
}

std::vector<answer> lookup_services(
    delegated_t tag,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options
) {
    return unwrap(try_lookup_services(tag, type, protocol, options));
}
#endif

result<std::vector<answer>> try_lookup_services(
    self_hosted_t,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options
) {
    return lookup(true, type, protocol, options);
}

result<std::vector<answer>> try_lookup_services(
    delegated_t,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options
) {
    return lookup(false, type, protocol, options);
}

} // namespace idfxx::mdns
