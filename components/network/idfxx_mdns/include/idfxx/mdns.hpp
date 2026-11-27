// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/mdns>
 * @file mdns.hpp
 * @brief Multicast DNS (mDNS) and DNS-SD: hostname, service advertisement, queries and browsing.
 *
 * @defgroup idfxx_mdns mDNS Component
 * @brief Advertises the device and its services on the local network, and discovers other hosts and
 * services.
 *
 * The device answers for a `.local` hostname, advertises DNS-SD services
 * (with TXT records and subtypes) for itself or on behalf of other hosts,
 * resolves hosts and services by one-shot queries (blocking, or as an
 * @ref idfxx::future), and follows services continuously with a
 * @ref idfxx::mdns::browser.
 *
 * The mDNS service is global: @ref idfxx::mdns::init starts it and
 * @ref idfxx::mdns::deinit stops it. It needs the system default event loop
 * and `esp_netif_init()`, and runs on the default Wi-Fi station, access point
 * and Ethernet interfaces without further setup.
 *
 * Depends on @ref idfxx_core for error handling and futures, @ref idfxx_event
 * for the hostname-changed event, @ref idfxx_netif for network interfaces and
 * @ref idfxx_net for address families and port numbers.
 *
 * @code
 * idfxx::mdns::init();
 * idfxx::mdns::set_hostname("my-device");
 *
 * idfxx::mdns::service web({
 *     .instance_name = "My Device",
 *     .type = "_http",
 *     .protocol = idfxx::mdns::protocol::tcp,
 *     .port = 80,
 *     .txt = {{.key = "path", .value = "/"}},
 * });
 *
 * for (const auto& peer : idfxx::mdns::query_ptr("_http", idfxx::mdns::protocol::tcp, 3s)) {
 *     // peer.instance_name, peer.hostname, peer.port, peer.ipv4_addresses, ...
 * }
 * @endcode
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/error>
#include <idfxx/event>
#include <idfxx/flags>
#include <idfxx/future>
#include <idfxx/net/endpoint>
#include <idfxx/net>
#include <idfxx/netif>
#include <idfxx/required>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <esp_err.h>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

/**
 * @headerfile <idfxx/mdns>
 * @brief Multicast DNS and DNS-SD.
 */
namespace idfxx::mdns {

// =============================================================================
// Errors
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief Error codes specific to mDNS.
 *
 * Other failures are reported with @ref idfxx::errc codes.
 */
enum class errc : esp_err_t {
    /// A configured capacity is exhausted: `CONFIG_MDNS_MAX_SERVICES` services,
    /// `CONFIG_MDNS_MAX_INTERFACES` interfaces, or the mDNS task's action queue
    /// (`CONFIG_MDNS_ACTION_QUEUE_LEN`). A full queue is transient, so retrying
    /// later can succeed.
    limit_reached = 1,
};

/**
 * @headerfile <idfxx/mdns>
 * @brief Error category for mDNS errors.
 */
class error_category : public std::error_category {
public:
    /**
     * @brief Returns the name of the error category.
     *
     * @return The category name.
     */
    [[nodiscard]] const char* name() const noexcept override final;

    /**
     * @brief Returns a human-readable message for the given error code.
     *
     * @param ec The error code value.
     *
     * @return A descriptive error message.
     */
    [[nodiscard]] std::string message(int ec) const override final;
};

/**
 * @headerfile <idfxx/mdns>
 * @brief Returns a reference to the mDNS error category singleton.
 *
 * @return Reference to the singleton mdns::error_category instance.
 */
[[nodiscard]] const error_category& mdns_category() noexcept;

/**
 * @headerfile <idfxx/mdns>
 * @brief Creates an error code from an idfxx::mdns::errc value.
 *
 * @param e The mDNS error code.
 *
 * @return The corresponding std::error_code in the mDNS category.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), mdns_category()};
}

// =============================================================================
// Enumerations
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief Transport protocol of a DNS-SD service.
 */
enum class protocol : uint8_t {
    // clang-format off
    tcp = 0, /*!< TCP, the `_tcp` label. */
    udp = 1, /*!< UDP, the `_udp` label. */
    // clang-format on
};

/**
 * @headerfile <idfxx/mdns>
 * @brief DNS record type requested by a query.
 */
enum class record_type : uint16_t {
    // clang-format off
    a    = 0x0001, /*!< IPv4 address of a host. */
    ptr  = 0x000C, /*!< Instances of a service type. */
    txt  = 0x0010, /*!< TXT record of a service instance. */
    aaaa = 0x001C, /*!< IPv6 address of a host. */
    srv  = 0x0021, /*!< Hostname and port of a service instance. */
    any  = 0x00FF, /*!< Any record type. */
    // clang-format on
};

/**
 * @headerfile <idfxx/mdns>
 * @brief How a query asks for its answers.
 */
enum class transmission : uint8_t {
    // clang-format off
    unicast   = 0, /*!< Ask responders to answer the querier directly. */
    multicast = 1, /*!< Ask responders to answer to the multicast group. */
    // clang-format on
};

/**
 * @headerfile <idfxx/mdns>
 * @brief Actions on a network interface, combined as `flags<netif_action>`.
 *
 * Used with @ref post_netif_action to enable, announce or disable mDNS on an
 * interface, for example from IP and connection event listeners.
 */
enum class netif_action : uint32_t {
    /// Enable mDNS over IPv4: probe, resolve conflicts and announce.
    enable_ipv4 = 1u << 1,
    /// Enable mDNS over IPv6: probe, resolve conflicts and announce.
    enable_ipv6 = 1u << 2,
    /// Announce the hostname and services over IPv4 again.
    announce_ipv4 = 1u << 3,
    /// Announce the hostname and services over IPv6 again.
    announce_ipv6 = 1u << 4,
    /// Disable mDNS over IPv4. mDNS sends no goodbye packets. An interface
    /// that was registered with @ref register_netif is no longer registered
    /// afterward (see @ref post_netif_action).
    disable_ipv4 = 1u << 5,
    /// Disable mDNS over IPv6. mDNS sends no goodbye packets. An interface
    /// that was registered with @ref register_netif is no longer registered
    /// afterward (see @ref post_netif_action).
    disable_ipv6 = 1u << 6,
    /// Answer reverse lookups for the interface's IPv4 address.
    ipv4_reverse_lookup = 1u << 7,
    /// Answer reverse lookups for the interface's IPv6 addresses.
    ipv6_reverse_lookup = 1u << 8,
};

} // namespace idfxx::mdns

/** @cond INTERNAL */
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::mdns::netif_action> = true;
/** @endcond */

namespace idfxx::mdns {

// =============================================================================
// Records
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief One key/value item of a TXT record.
 *
 * Values are binary-safe: any bytes, including zeros, may appear in
 * @ref value. An item without a value is a boolean attribute and is sent as
 * the bare key. An item with an empty value is sent as `key=`.
 *
 * The key must not be empty or contain `=` or a NUL character. The whole item
 * (the key, the `=` and the value) must fit in 255 bytes. The key has no
 * shorter limit of its own.
 */
struct txt_item {
    /** @brief Item key. */
    std::string key;
    /** @brief Item value, or `std::nullopt` for a key-only item. */
    std::optional<std::string> value = std::nullopt;

    /** @brief Compares two items for equality. */
    [[nodiscard]] bool operator==(const txt_item&) const = default;
};

/**
 * @headerfile <idfxx/mdns>
 * @brief A host or service record found by a query, a lookup or a browser.
 *
 * Which fields are present depends on the record type asked for and on what
 * the responders sent. A field that was not received is `std::nullopt` (or
 * empty, for the lists).
 */
struct answer {
    /** @brief Service instance name, e.g. `"Living Room Printer"`. */
    std::optional<std::string> instance_name = std::nullopt;
    /** @brief Service type, e.g. `"_http"`. */
    std::optional<std::string> service_type = std::nullopt;
    /** @brief Service protocol. */
    std::optional<enum protocol> protocol = std::nullopt;
    /** @brief Hostname of the host providing the service, without `.local`. */
    std::optional<std::string> hostname = std::nullopt;
    /** @brief Service port. */
    std::optional<net::port_number> port = std::nullopt;
    /** @brief TXT record items. */
    std::vector<txt_item> txt = {};
    /** @brief IPv4 addresses of the host. */
    std::vector<net::ipv4_addr> ipv4_addresses = {};
    /** @brief IPv6 addresses of the host. */
    std::vector<net::ipv6_addr> ipv6_addresses = {};
    /** @brief Subtype the browser matched, for a browser with a subtype. */
    std::optional<std::string> subtype = std::nullopt;
    /**
     * @brief Key of the network interface the answer arrived on.
     *
     * Use @ref idfxx::netif::find_by_key to get the interface. Absent for
     * local lookups.
     */
    std::optional<std::string> netif_key = std::nullopt;
    /** @brief Address family the answer arrived over. Absent for local lookups. */
    std::optional<net::address_family> family = std::nullopt;
    /**
     * @brief Time to live of the record.
     *
     * A browser reports a service that has gone away with a TTL of zero.
     */
    std::chrono::seconds ttl{0};

    /** @brief Compares two answers for equality. */
    [[nodiscard]] bool operator==(const answer&) const = default;
};

// =============================================================================
// Events
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief Event IDs posted by the mDNS service.
 */
enum class event_id : uint8_t {
    hostname_changed, ///< The device's hostname changed (carries @ref hostname_info).
};

/**
 * @headerfile <idfxx/mdns>
 * @brief Data of the @ref hostname_changed event.
 */
struct hostname_info {
    /**
     * @brief The new hostname, NUL-terminated.
     *
     * Holds every hostname @ref set_hostname accepts (at most 63
     * characters, one DNS label). A hostname longer than 64 characters,
     * which only a `CONFIG_MDNS_RESPOND_REVERSE_QUERIES` build with IPv6
     * allows, is truncated.
     */
    char hostname[65];
};

/**
 * @brief Event base for mDNS events.
 *
 * Events are posted to the system default event loop:
 *
 * @code
 * idfxx::event_loop::system().listener_add(idfxx::mdns::hostname_changed,
 *     [](const idfxx::mdns::hostname_info& info) {
 *         // info.hostname
 *     });
 * @endcode
 */
IDFXX_EVENT_DEFINE_BASE(mdns_events, event_id);

/**
 * @brief Hostname-changed event, carrying the new hostname.
 *
 * Posted to the system default event loop whenever the hostname changes:
 * after @ref set_hostname, and when mDNS renames the device to resolve a
 * conflict with another host on the network. The event is dropped if the
 * event loop's queue is full.
 */
inline constexpr event<event_id, hostname_info> hostname_changed{event_id::hostname_changed};

// =============================================================================
// Lifecycle
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Starts the mDNS service.
 *
 * Must be called before any other mDNS function. Calling it again while the
 * service is running has no effect.
 *
 * The system default event loop must exist and `esp_netif_init()` must have
 * been called. mDNS then runs on the default Wi-Fi station, access point and
 * Ethernet interfaces, as enabled by `CONFIG_MDNS_PREDEF_NETIF_*`.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with idfxx::errc::fail if the
 *         service could not start, for example because the system default
 *         event loop does not exist.
 */
void init();
#endif

/**
 * @brief Starts the mDNS service.
 *
 * Must be called before any other mDNS function. Calling it again while the
 * service is running has no effect.
 *
 * The system default event loop must exist and `esp_netif_init()` must have
 * been called. mDNS then runs on the default Wi-Fi station, access point and
 * Ethernet interfaces, as enabled by `CONFIG_MDNS_PREDEF_NETIF_*`.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::fail If the service could not start, for example
 *         because the system default event loop does not exist.
 */
[[nodiscard]] result<void> try_init();

/**
 * @brief Stops the mDNS service and frees its resources.
 *
 * Removes every advertised service, delegated host and browser, and forgets
 * registered interfaces. It first waits for queries started with
 * @ref start_query that are still in progress, so their futures complete
 * with their answers. It can therefore block for up to the longest remaining
 * query timeout. Does nothing if the service is not running.
 *
 * Services, delegated hosts and browsers must not outlive the service:
 * destroying one after `deinit()` has no effect, but using one is an error.
 * Futures from @ref start_query stay valid.
 *
 * `deinit()` never fails.
 *
 * @note No other idfxx::mdns call may be in progress on another task when
 *       `deinit()` runs. In particular, a blocking query must have returned.
 * @note Must not be called from a browser callback.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so waiting for them there would
 *       never return.
 */
void deinit();

// =============================================================================
// Hostname and default instance name
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sets the device's hostname.
 *
 * The device answers for `<hostname>.local`. A hostname is required before
 * services can be advertised for the device. Services already advertised
 * move to the new hostname. Posts @ref hostname_changed.
 *
 * @param hostname The hostname, without `.local`; 1 to 63 characters. Must
 *        not be the name of a @ref delegated_host.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running, or the
 *           hostname is the name of a delegated host (compared without regard
 *           to case).
 *         - idfxx::errc::invalid_arg if the hostname is empty, is longer than
 *           63 characters or contains a NUL character.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 */
void set_hostname(std::string_view hostname);

/**
 * @brief Returns the device's hostname.
 *
 * @return The hostname, without `.local`.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with idfxx::errc::invalid_state if
 *         the service is not running, or no hostname is set.
 */
[[nodiscard]] std::string get_hostname();

/**
 * @brief Sets the default instance name.
 *
 * Services created without an instance name use the default instance name,
 * or the hostname if there is none.
 *
 * @param instance_name The instance name; 1 to 63 characters.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running, or no
 *           hostname is set.
 *         - idfxx::errc::invalid_arg if the name is empty, is longer than 63
 *           characters or contains a NUL character.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 */
void set_instance_name(std::string_view instance_name);
#endif

/**
 * @brief Sets the device's hostname.
 *
 * The device answers for `<hostname>.local`. A hostname is required before
 * services can be advertised for the device. Services already advertised
 * move to the new hostname. Posts @ref hostname_changed.
 *
 * @param hostname The hostname, without `.local`; 1 to 63 characters. Must
 *        not be the name of a @ref delegated_host.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running, or the
 *         hostname is the name of a delegated host (compared without regard
 *         to case).
 * @retval idfxx::errc::invalid_arg If the hostname is empty, is longer than
 *         63 characters or contains a NUL character.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 */
[[nodiscard]] result<void> try_set_hostname(std::string_view hostname);

/**
 * @brief Returns the device's hostname.
 *
 * @return The hostname, without `.local`, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running, or no
 *         hostname is set.
 */
[[nodiscard]] result<std::string> try_get_hostname();

/**
 * @brief Sets the default instance name.
 *
 * Services created without an instance name use the default instance name,
 * or the hostname if there is none.
 *
 * @param instance_name The instance name; 1 to 63 characters.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running, or no
 *         hostname is set.
 * @retval idfxx::errc::invalid_arg If the name is empty, is longer than 63
 *         characters or contains a NUL character.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 */
[[nodiscard]] result<void> try_set_instance_name(std::string_view instance_name);

/**
 * @brief Checks whether a hostname is the device's own or a delegated host's.
 *
 * @param hostname The hostname to check, without `.local`. Compared without
 *        regard to case.
 *
 * @return `true` if the hostname is the device's hostname or the name of a
 *         @ref delegated_host. `false` otherwise, and always when the service
 *         is not running or the hostname is not a valid name (empty, longer
 *         than 63 bytes, or with a NUL character).
 */
[[nodiscard]] bool hostname_exists(std::string_view hostname);

// =============================================================================
// Network interfaces
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Runs mDNS on an additional network interface.
 *
 * mDNS handles the default Wi-Fi station, access point and Ethernet
 * interfaces automatically when their `CONFIG_MDNS_PREDEF_NETIF_*` options
 * are on. Other interfaces must be registered, and then enabled with
 * @ref post_netif_action (typically from IP event listeners). The interface
 * must stay alive until it is unregistered or the service stops.
 *
 * A disable action for either IP version (netif_action::disable_ipv4 or
 * netif_action::disable_ipv6) unregisters a registered interface when the
 * mDNS task runs it. Register the interface again before you enable mDNS on
 * it. An interface that mDNS handles automatically needs no registration
 * after a disable: mDNS finds it again by its interface key. Do not register
 * such an interface.
 *
 * @param iface The interface to register.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running, or the
 *           interface is already registered.
 *         - idfxx::mdns::errc::limit_reached if all
 *           `CONFIG_MDNS_MAX_INTERFACES` interface slots are in use. The
 *           default interfaces enabled by `CONFIG_MDNS_PREDEF_NETIF_*` each
 *           take a slot.
 */
void register_netif(netif::interface& iface);

/**
 * @brief Stops running mDNS on a registered network interface.
 *
 * Disables mDNS on the interface for both IP versions and frees its
 * interface slot. mDNS sends no goodbye packets. The call waits until the
 * mDNS task has run the disable, for at most about 1 second. After it
 * returns, @ref post_netif_action fails with idfxx::errc::invalid_state for
 * the interface until it is registered again.
 *
 * Only an interface that was registered with @ref register_netif can be
 * unregistered. This call never disables an interface that mDNS handles
 * automatically. A disable action for either IP version
 * (netif_action::disable_ipv4 or netif_action::disable_ipv6) unregisters the
 * interface already. After it runs, this call fails with
 * idfxx::errc::not_found for the interface.
 *
 * @param iface The interface registered with @ref register_netif.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::not_found if the interface is not registered. This
 *           includes an interface that a disable action has unregistered,
 *           and an interface that mDNS handles automatically.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task did not run the
 *           disable within about 1 second. Most likely the action was dropped
 *           because the mDNS task's action queue was full. The interface stays
 *           registered, and retrying later can succeed.
 */
void unregister_netif(netif::interface& iface);

/**
 * @brief Enables, announces or disables mDNS on a network interface.
 *
 * The actions run asynchronously on the mDNS task. If the task's action
 * queue is full, they are dropped without an error.
 *
 * The disable actions send no goodbye packets. A disable action for either
 * IP version also unregisters an interface that was registered with
 * @ref register_netif. After that, this call fails with
 * idfxx::errc::invalid_state for the interface until it is registered again.
 * An interface that mDNS handles automatically stays available: mDNS finds
 * it again by its interface key.
 *
 * @param iface An interface registered with @ref register_netif, or one of
 *        the default interfaces.
 * @param actions The actions to take.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with idfxx::errc::invalid_state if
 *         the service is not running, or mDNS does not run on the interface.
 *
 * @code
 * idfxx::mdns::register_netif(my_netif);
 * idfxx::mdns::post_netif_action(my_netif,
 *     idfxx::mdns::netif_action::enable_ipv4 | idfxx::mdns::netif_action::announce_ipv4);
 * @endcode
 */
void post_netif_action(netif::interface& iface, flags<netif_action> actions);
#endif

/**
 * @brief Runs mDNS on an additional network interface.
 *
 * mDNS handles the default Wi-Fi station, access point and Ethernet
 * interfaces automatically when their `CONFIG_MDNS_PREDEF_NETIF_*` options
 * are on. Other interfaces must be registered, and then enabled with
 * @ref try_post_netif_action (typically from IP event listeners). The
 * interface must stay alive until it is unregistered or the service stops.
 *
 * A disable action for either IP version (netif_action::disable_ipv4 or
 * netif_action::disable_ipv6) unregisters a registered interface when the
 * mDNS task runs it. Register the interface again before you enable mDNS on
 * it. An interface that mDNS handles automatically needs no registration
 * after a disable: mDNS finds it again by its interface key. Do not register
 * such an interface.
 *
 * @param iface The interface to register.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running, or the
 *         interface is already registered.
 * @retval idfxx::mdns::errc::limit_reached If all `CONFIG_MDNS_MAX_INTERFACES`
 *         interface slots are in use. The default interfaces enabled by
 *         `CONFIG_MDNS_PREDEF_NETIF_*` each take a slot.
 */
[[nodiscard]] result<void> try_register_netif(netif::interface& iface);

/**
 * @brief Stops running mDNS on a registered network interface.
 *
 * Disables mDNS on the interface for both IP versions and frees its
 * interface slot. mDNS sends no goodbye packets. The call waits until the
 * mDNS task has run the disable, for at most about 1 second. After it
 * returns, @ref try_post_netif_action fails with idfxx::errc::invalid_state
 * for the interface until it is registered again.
 *
 * Only an interface that was registered with @ref try_register_netif can be
 * unregistered. This call never disables an interface that mDNS handles
 * automatically. A disable action for either IP version
 * (netif_action::disable_ipv4 or netif_action::disable_ipv6) unregisters the
 * interface already. After it runs, this call fails with
 * idfxx::errc::not_found for the interface.
 *
 * @param iface The interface registered with @ref try_register_netif.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::not_found If the interface is not registered. This
 *         includes an interface that a disable action has unregistered, and
 *         an interface that mDNS handles automatically.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task did not run the
 *         disable within about 1 second. Most likely the action was dropped
 *         because the mDNS task's action queue was full. The interface stays
 *         registered, and retrying later can succeed.
 */
result<void> try_unregister_netif(netif::interface& iface);

/**
 * @brief Enables, announces or disables mDNS on a network interface.
 *
 * The actions run asynchronously on the mDNS task. If the task's action
 * queue is full, they are dropped without an error.
 *
 * The disable actions send no goodbye packets. A disable action for either
 * IP version also unregisters an interface that was registered with
 * @ref try_register_netif. After that, this call fails with
 * idfxx::errc::invalid_state for the interface until it is registered again.
 * An interface that mDNS handles automatically stays available: mDNS finds
 * it again by its interface key.
 *
 * @param iface An interface registered with @ref try_register_netif, or one
 *        of the default interfaces.
 * @param actions The actions to take.
 *
 * @return Success, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running, or mDNS
 *         does not run on the interface.
 */
[[nodiscard]] result<void>
try_post_netif_action(netif::interface& iface, flags<netif_action> actions);

// =============================================================================
// Delegated hosts
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief A hostname the device answers for on behalf of another host.
 *
 * The device answers A and AAAA queries for `<name>.local` with the given
 * addresses, and can advertise services on the host (see @ref service). This
 * lets the device publish hosts and services that cannot speak mDNS
 * themselves.
 *
 * The destructor removes the host and every service advertised on it. The
 * host must outlive the @ref service objects created on it: destroy them
 * first.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 *
 * @code
 * idfxx::mdns::delegated_host printer({
 *     .name = "office-printer",
 *     .ipv4_addresses = {idfxx::net::ipv4_addr(192, 168, 1, 50)},
 * });
 * idfxx::mdns::service ipp(printer, {
 *     .type = "_ipp",
 *     .protocol = idfxx::mdns::protocol::tcp,
 *     .port = 631,
 * });
 * @endcode
 */
class delegated_host {
public:
    /**
     * @headerfile <idfxx/mdns>
     * @brief Configuration for a delegated host.
     *
     * At least one address is required.
     */
    struct config {
        /** @brief Hostname, without `.local`; 1 to 63 characters. */
        required<std::string> name;
        /** @brief IPv4 addresses to answer with. */
        std::vector<net::ipv4_addr> ipv4_addresses = {};
        /** @brief IPv6 addresses to answer with. */
        std::vector<net::ipv6_addr> ipv6_addresses = {};

        /** @brief Compares two configurations for equality. */
        [[nodiscard]] bool operator==(const config&) const = default;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Adds a delegated host.
     *
     * @param cfg Host configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           the name is the device's hostname or another delegated host's.
     *         - idfxx::errc::invalid_arg if the name is empty, is longer than 63
     *           characters or contains a NUL character, or there are no
     *           addresses.
     *         - idfxx::errc::not_supported if addresses are given for an IP
     *           version that lwIP is built without.
     *         - idfxx::mdns::errc::limit_reached if the mDNS task's action
     *           queue is full. Retrying later can succeed.
     */
    [[nodiscard]] explicit delegated_host(config cfg);
#endif

    /**
     * @brief Adds a delegated host.
     *
     * @param cfg Host configuration.
     *
     * @return The host, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         the name is the device's hostname or another delegated host's.
     * @retval idfxx::errc::invalid_arg If the name is empty, is longer than
     *         63 characters or contains a NUL character, or there are no
     *         addresses.
     * @retval idfxx::errc::not_supported If addresses are given for an IP
     *         version that lwIP is built without.
     * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action
     *         queue is full. Retrying later can succeed.
     */
    [[nodiscard]] static result<delegated_host> make(config cfg);

    /**
     * @brief Removes the host and the services advertised on it.
     *
     * Removal completes asynchronously on the mDNS task. The @ref service
     * objects created on the host must be destroyed before it.
     */
    ~delegated_host();

    delegated_host(const delegated_host&) = delete;
    delegated_host& operator=(const delegated_host&) = delete;

    /**
     * @brief Move constructor.
     *
     * @param other The host to move from.
     */
    delegated_host(delegated_host&& other) noexcept;

    /**
     * @brief Move assignment. Removes the host this object held first.
     *
     * @param other The host to move from.
     * @return Reference to this.
     */
    delegated_host& operator=(delegated_host&& other) noexcept;

    /**
     * @brief Returns the host's name.
     *
     * @return The hostname, without `.local`, as given in the configuration.
     *
     * @note A moved-from object must not be used: calling this on one is
     *       undefined behavior.
     */
    [[nodiscard]] const std::string& name() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Replaces the host's addresses.
     *
     * The change completes asynchronously on the mDNS task.
     *
     * @param ipv4_addresses New IPv4 addresses.
     * @param ipv6_addresses New IPv6 addresses.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if both lists are empty.
     *         - idfxx::errc::not_supported if addresses are given for an IP
     *           version that lwIP is built without.
     *         - idfxx::mdns::errc::limit_reached if the mDNS task's action
     *           queue is full. Retrying later can succeed.
     */
    void set_addresses(
        std::span<const net::ipv4_addr> ipv4_addresses,
        std::span<const net::ipv6_addr> ipv6_addresses
    ) {
        unwrap(try_set_addresses(ipv4_addresses, ipv6_addresses));
    }
#endif

    /**
     * @brief Replaces the host's addresses.
     *
     * The change completes asynchronously on the mDNS task.
     *
     * @param ipv4_addresses New IPv4 addresses.
     * @param ipv6_addresses New IPv6 addresses.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If both lists are empty.
     * @retval idfxx::errc::not_supported If addresses are given for an IP
     *         version that lwIP is built without.
     * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action
     *         queue is full. Retrying later can succeed.
     */
    [[nodiscard]] result<void> try_set_addresses(
        std::span<const net::ipv4_addr> ipv4_addresses,
        std::span<const net::ipv6_addr> ipv6_addresses
    );

private:
    /** @cond INTERNAL */
    friend class service;
    struct state;
    explicit delegated_host(std::unique_ptr<state> s) noexcept;
    void _release() noexcept;
    /** @endcond */

    std::unique_ptr<state> _state;
};

// =============================================================================
// Services
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief An advertised DNS-SD service.
 *
 * Advertises a service instance (`<instance>._<type>._<protocol>.local`) with
 * its port, TXT record and subtypes, for the device itself or for a
 * @ref delegated_host. The device's hostname must be set first (see
 * @ref set_hostname), also to advertise for a delegated host.
 *
 * The destructor stops advertising the service.
 *
 * A service without an instance name must be the only service of its type
 * and protocol on its host. To advertise several, name them all. (The device
 * and each delegated host are separate hosts.)
 *
 * A service without an instance name can't remove a subtype. For such a
 * service, @ref remove_subtype fails with idfxx::errc::not_supported.
 * @ref set_subtypes fails with the same error when the new subtypes leave out
 * one that the service has.
 *
 * Some calls send goodbye packets from the calling task and use more of its
 * stack:
 * - A call that removes a subtype uses about 2.2 KB.
 * - The destructor uses about 1.5 KB.
 *
 * The system event task has 2304 bytes by default
 * (`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`), and the event loop uses part
 * of it. A listener on the system event loop that makes these calls needs a
 * larger stack.
 *
 * mDNS renames an instance by itself when another host on the network
 * already uses its name. A service that is the only one of its type and
 * protocol on its host keeps working after that. The exception is
 * @ref remove_subtype and @ref set_subtypes on a named service. These calls
 * find the service by its instance name, so after the rename they fail with
 * idfxx::errc::not_found. A call to @ref set_instance_name gives the service
 * a name that they find again.
 *
 * A named service that shares its type and protocol with another can no
 * longer be changed or removed once the network has renamed it. Its calls
 * fail with idfxx::errc::not_found, and its destructor can't remove it. It
 * stays advertised until @ref deinit, or until its delegated host is
 * destroyed. While it stays advertised, no service without an instance name
 * can be created with its type and protocol on its host.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 *
 * @code
 * idfxx::mdns::service web({
 *     .instance_name = "Kitchen Display",
 *     .type = "_http",
 *     .protocol = idfxx::mdns::protocol::tcp,
 *     .port = 80,
 *     .txt = {{.key = "path", .value = "/"}, {.key = "secure"}},
 * });
 * web.set_txt_item({.key = "version", .value = "2"});
 * @endcode
 */
class service {
public:
    /**
     * @headerfile <idfxx/mdns>
     * @brief Configuration for a service.
     */
    struct config {
        /**
         * @brief Instance name, e.g. `"Kitchen Display"`; 1 to 63 characters.
         *
         * When `std::nullopt`, the service uses the default instance name (see
         * @ref set_instance_name), or the hostname if there is none. A service
         * without an instance name must be the only service of its type and
         * protocol on its host. To advertise several, name them all.
         */
        std::optional<std::string> instance_name = std::nullopt;
        /** @brief Service type, e.g. `"_http"`; 1 to 63 characters. */
        required<std::string> type;
        /** @brief Service protocol. */
        required<enum protocol> protocol;
        /** @brief Port the service listens on; must not be zero. */
        required<net::port_number> port;
        /** @brief TXT record items; at most 255. */
        std::vector<txt_item> txt = {};
        /**
         * @brief Subtypes, e.g. `"_printer"`; at most 255, each 1 to 63 characters.
         *
         * No two subtypes may be the same. Subtypes that differ only in case
         * are different.
         */
        std::vector<std::string> subtypes = {};

        /** @brief Compares two configurations for equality. */
        [[nodiscard]] bool operator==(const config&) const = default;
    };

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Advertises a service for the device.
     *
     * @param cfg Service configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           no hostname is set. Also if a service of the same type and
     *           protocol is already advertised for the device with the same
     *           instance name (compared without regard to case), or without an
     *           instance name. Also if this service has no instance name, and
     *           another service of its type and protocol is advertised for the
     *           device, including a renamed service that its destructor could
     *           not remove (see @ref service).
     *         - idfxx::errc::invalid_arg if the type or instance name is
     *           empty, is longer than 63 characters or contains a NUL
     *           character, the port is zero, a TXT item or subtype is invalid,
     *           or two subtypes are the same.
     *         - idfxx::mdns::errc::limit_reached if `CONFIG_MDNS_MAX_SERVICES`
     *           services are already advertised.
     */
    [[nodiscard]] explicit service(config cfg);

    /**
     * @brief Advertises a service on a delegated host.
     *
     * The device's hostname must be set first (see @ref set_hostname).
     *
     * @param host The host providing the service. It must outlive the service.
     * @param cfg Service configuration.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, the
     *           device has no hostname, or the host was created before the last
     *           @ref deinit. Also if a service of the same type and protocol is
     *           already advertised on the host with the same instance name
     *           (compared without regard to case), or without an instance
     *           name. Also if this service has no instance name, and another
     *           service of its type and protocol is advertised on the host,
     *           including a renamed service that its destructor could not
     *           remove (see @ref service).
     *         - idfxx::errc::invalid_arg if the type or instance name is
     *           empty, is longer than 63 characters or contains a NUL
     *           character, the port is zero, a TXT item or subtype is invalid,
     *           or two subtypes are the same.
     *         - idfxx::mdns::errc::limit_reached if `CONFIG_MDNS_MAX_SERVICES`
     *           services are already advertised.
     */
    [[nodiscard]] explicit service(delegated_host& host, config cfg);
#endif

    /**
     * @brief Advertises a service for the device.
     *
     * @param cfg Service configuration.
     *
     * @return The service, or an error.
     * @retval idfxx::errc::invalid_state If any of the following is true:
     *         - The service is not running, or no hostname is set.
     *         - A service of the same type and protocol is already advertised
     *           for the device with the same instance name (compared without
     *           regard to case), or without an instance name.
     *         - This service has no instance name, and another service of its
     *           type and protocol is advertised for the device. This includes a
     *           renamed service that its destructor could not remove (see
     *           @ref service).
     * @retval idfxx::errc::invalid_arg If the type or instance name is empty,
     *         is longer than 63 characters or contains a NUL character, the
     *         port is zero, a TXT item or subtype is invalid, or two subtypes
     *         are the same.
     * @retval idfxx::mdns::errc::limit_reached If `CONFIG_MDNS_MAX_SERVICES`
     *         services are already advertised.
     */
    [[nodiscard]] static result<service> make(config cfg);

    /**
     * @brief Advertises a service on a delegated host.
     *
     * The device's hostname must be set first (see @ref set_hostname).
     *
     * @param host The host providing the service. It must outlive the service.
     * @param cfg Service configuration.
     *
     * @return The service, or an error.
     * @retval idfxx::errc::invalid_state If any of the following is true:
     *         - The service is not running, or the device has no hostname.
     *         - The host was created before the last @ref deinit.
     *         - A service of the same type and protocol is already advertised
     *           on the host with the same instance name (compared without
     *           regard to case), or without an instance name.
     *         - This service has no instance name, and another service of its
     *           type and protocol is advertised on the host. This includes a
     *           renamed service that its destructor could not remove (see
     *           @ref service).
     * @retval idfxx::errc::invalid_arg If the type or instance name is empty,
     *         is longer than 63 characters or contains a NUL character, the
     *         port is zero, a TXT item or subtype is invalid, or two subtypes
     *         are the same.
     * @retval idfxx::mdns::errc::limit_reached If `CONFIG_MDNS_MAX_SERVICES`
     *         services are already advertised.
     */
    [[nodiscard]] static result<service> make(delegated_host& host, config cfg);

    /** @brief Stops advertising the service. */
    ~service();

    service(const service&) = delete;
    service& operator=(const service&) = delete;

    /**
     * @brief Move constructor.
     *
     * @param other The service to move from.
     */
    service(service&& other) noexcept;

    /**
     * @brief Move assignment. Stops advertising the service this object held first.
     *
     * @param other The service to move from.
     * @return Reference to this.
     */
    service& operator=(service&& other) noexcept;

    /**
     * @brief Returns the service type.
     *
     * @return The service type, e.g. `"_http"`, as given in the configuration.
     *
     * @note A moved-from object must not be used: calling this on one is
     *       undefined behavior.
     */
    [[nodiscard]] const std::string& type() const noexcept;

    /**
     * @brief Returns the service protocol.
     *
     * @return The protocol, as given in the configuration.
     *
     * @note A moved-from object must not be used: calling this on one is
     *       undefined behavior.
     */
    [[nodiscard]] enum protocol protocol() const noexcept;

    /**
     * @brief Returns the service's explicit instance name.
     *
     * @return The instance name given in the configuration or by the last
     *         successful @ref set_instance_name, or `std::nullopt` when the
     *         service uses the default instance name.
     *
     * @note A moved-from object must not be used: calling this on one is
     *       undefined behavior.
     */
    [[nodiscard]] const std::optional<std::string>& instance_name() const noexcept;

    /**
     * @brief Returns the service's port.
     *
     * @return The port given in the configuration or by the last successful
     *         @ref set_port.
     *
     * @note A moved-from object must not be used: calling this on one is
     *       undefined behavior.
     */
    [[nodiscard]] net::port_number port() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Changes the service's port.
     *
     * @param port The new port; must not be zero.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if the port is zero.
     *         - idfxx::errc::not_found if the service's delegated host was
     *           destroyed first, or the network has renamed the service while
     *           it shares its type and protocol with another (see
     *           @ref service).
     */
    void set_port(net::port_number port) { unwrap(try_set_port(port)); }

    /**
     * @brief Changes the service's instance name.
     *
     * @param instance_name The new instance name; 1 to 63 characters.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, this
     *           object was created before the last @ref deinit, or another
     *           service of the same type and protocol on the host has the name
     *           (compared without regard to case).
     *         - idfxx::errc::invalid_arg if the name is empty, is longer than
     *           63 characters or contains a NUL character.
     *         - idfxx::errc::not_found if the service's delegated host was
     *           destroyed first, or the network has renamed the service while
     *           it shares its type and protocol with another (see
     *           @ref service).
     */
    void set_instance_name(std::string_view instance_name) {
        unwrap(try_set_instance_name(instance_name));
    }

    /**
     * @brief Replaces the service's whole TXT record.
     *
     * @param items The new items; at most 255. Empty clears the record.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if there are too many items, or an
     *           item is invalid (see @ref txt_item).
     *         - idfxx::errc::not_found if the service's delegated host was
     *           destroyed first, or the network has renamed the service while
     *           it shares its type and protocol with another (see
     *           @ref service).
     */
    void set_txt(std::span<const txt_item> items) { unwrap(try_set_txt(items)); }

    /**
     * @brief Adds a TXT item, or replaces the item with the same key.
     *
     * @param item The item.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if the item is invalid (see
     *           @ref txt_item).
     *         - idfxx::errc::not_found if the service's delegated host was
     *           destroyed first, or the network has renamed the service while
     *           it shares its type and protocol with another (see
     *           @ref service).
     */
    void set_txt_item(const txt_item& item) { unwrap(try_set_txt_item(item)); }

    /**
     * @brief Removes the TXT item with a key.
     *
     * Does nothing if the record has no item with the key.
     *
     * @param key The item's key.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if the key is empty or contains a NUL
     *           character.
     *         - idfxx::errc::not_found if the service's delegated host was
     *           destroyed first, or the network has renamed the service while
     *           it shares its type and protocol with another (see
     *           @ref service).
     */
    void remove_txt_item(std::string_view key) { unwrap(try_remove_txt_item(key)); }

    /**
     * @brief Adds a subtype.
     *
     * @param subtype The subtype, e.g. `"_printer"`; 1 to 63 characters.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if the subtype is empty, is longer
     *           than 63 characters, contains a NUL character or is already
     *           present.
     *         - idfxx::errc::not_found if the service's delegated host was
     *           destroyed first, or the network has renamed the service while
     *           it shares its type and protocol with another (see
     *           @ref service).
     */
    void add_subtype(std::string_view subtype) { unwrap(try_add_subtype(subtype)); }

    /**
     * @brief Removes a subtype.
     *
     * Only a service with an instance name can remove a subtype. To give the
     * service an instance name, set `config::instance_name` or call
     * @ref set_instance_name.
     *
     * @param subtype The subtype.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if the subtype is empty, is longer
     *           than 63 characters or contains a NUL character.
     *         - idfxx::errc::not_supported if the service has no instance
     *           name.
     *         - idfxx::errc::not_found if the service has no such subtype, the
     *           service's delegated host was destroyed first, or the network
     *           has renamed the service (see @ref service).
     */
    void remove_subtype(std::string_view subtype) { unwrap(try_remove_subtype(subtype)); }

    /**
     * @brief Replaces all of the service's subtypes.
     *
     * Only a service with an instance name can remove a subtype. A service
     * without one can only keep its subtypes and add new ones. To give the
     * service an instance name, set `config::instance_name` or call
     * @ref set_instance_name.
     *
     * @param subtypes The new subtypes; at most 255, each 1 to 63 characters.
     *        No two may be the same. Subtypes that differ only in case are
     *        different. Empty removes them all.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or
     *           this object was created before the last @ref deinit.
     *         - idfxx::errc::invalid_arg if there are too many subtypes, one
     *           is empty, is longer than 63 characters or contains a NUL
     *           character, or two are the same.
     *         - idfxx::errc::not_supported if the service has no instance name,
     *           and the new subtypes leave out one that it has.
     *         - idfxx::errc::not_found if the service's delegated host was
     *           destroyed first, or the network has renamed the service (see
     *           @ref service).
     */
    void set_subtypes(std::span<const std::string> subtypes) { unwrap(try_set_subtypes(subtypes)); }
#endif

    /**
     * @brief Changes the service's port.
     *
     * @param port The new port; must not be zero.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If the port is zero.
     * @retval idfxx::errc::not_found If the service's delegated host was
     *         destroyed first, or the network has renamed the service while it
     *         shares its type and protocol with another (see @ref service).
     */
    [[nodiscard]] result<void> try_set_port(net::port_number port);

    /**
     * @brief Changes the service's instance name.
     *
     * @param instance_name The new instance name; 1 to 63 characters.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, this
     *         object was created before the last @ref deinit, or another
     *         service of the same type and protocol on the host has the name
     *         (compared without regard to case).
     * @retval idfxx::errc::invalid_arg If the name is empty, is longer than 63
     *         characters or contains a NUL character.
     * @retval idfxx::errc::not_found If the service's delegated host was
     *         destroyed first, or the network has renamed the service while it
     *         shares its type and protocol with another (see @ref service).
     */
    [[nodiscard]] result<void> try_set_instance_name(std::string_view instance_name);

    /**
     * @brief Replaces the service's whole TXT record.
     *
     * @param items The new items; at most 255. Empty clears the record.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If there are too many items, or an item
     *         is invalid (see @ref txt_item).
     * @retval idfxx::errc::not_found If the service's delegated host was
     *         destroyed first, or the network has renamed the service while it
     *         shares its type and protocol with another (see @ref service).
     */
    [[nodiscard]] result<void> try_set_txt(std::span<const txt_item> items);

    /**
     * @brief Adds a TXT item, or replaces the item with the same key.
     *
     * @param item The item.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If the item is invalid (see @ref txt_item).
     * @retval idfxx::errc::not_found If the service's delegated host was
     *         destroyed first, or the network has renamed the service while it
     *         shares its type and protocol with another (see @ref service).
     */
    [[nodiscard]] result<void> try_set_txt_item(const txt_item& item);

    /**
     * @brief Removes the TXT item with a key.
     *
     * Does nothing if the record has no item with the key.
     *
     * @param key The item's key.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If the key is empty or contains a NUL
     *         character.
     * @retval idfxx::errc::not_found If the service's delegated host was
     *         destroyed first, or the network has renamed the service while it
     *         shares its type and protocol with another (see @ref service).
     */
    [[nodiscard]] result<void> try_remove_txt_item(std::string_view key);

    /**
     * @brief Adds a subtype.
     *
     * @param subtype The subtype, e.g. `"_printer"`; 1 to 63 characters.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If the subtype is empty, is longer than
     *         63 characters, contains a NUL character or is already present.
     * @retval idfxx::errc::not_found If the service's delegated host was
     *         destroyed first, or the network has renamed the service while it
     *         shares its type and protocol with another (see @ref service).
     */
    [[nodiscard]] result<void> try_add_subtype(std::string_view subtype);

    /**
     * @brief Removes a subtype.
     *
     * Only a service with an instance name can remove a subtype. To give the
     * service an instance name, set `config::instance_name` or call
     * @ref try_set_instance_name.
     *
     * @param subtype The subtype.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If the subtype is empty, is longer than
     *         63 characters or contains a NUL character.
     * @retval idfxx::errc::not_supported If the service has no instance name.
     * @retval idfxx::errc::not_found If the service has no such subtype, the
     *         service's delegated host was destroyed first, or the network has
     *         renamed the service (see @ref service).
     */
    [[nodiscard]] result<void> try_remove_subtype(std::string_view subtype);

    /**
     * @brief Replaces all of the service's subtypes.
     *
     * Only a service with an instance name can remove a subtype. A service
     * without one can only keep its subtypes and add new ones. To give the
     * service an instance name, set `config::instance_name` or call
     * @ref try_set_instance_name.
     *
     * @param subtypes The new subtypes; at most 255, each 1 to 63 characters.
     *        No two may be the same. Subtypes that differ only in case are
     *        different. Empty removes them all.
     *
     * @return Success, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or
     *         this object was created before the last @ref deinit.
     * @retval idfxx::errc::invalid_arg If there are too many subtypes, one is
     *         empty, is longer than 63 characters or contains a NUL character,
     *         or two are the same.
     * @retval idfxx::errc::not_supported If the service has no instance name,
     *         and the new subtypes leave out one that it has.
     * @retval idfxx::errc::not_found If the service's delegated host was
     *         destroyed first, or the network has renamed the service (see
     *         @ref service).
     */
    [[nodiscard]] result<void> try_set_subtypes(std::span<const std::string> subtypes);

private:
    /** @cond INTERNAL */
    struct state;
    explicit service(std::unique_ptr<state> s) noexcept;
    [[nodiscard]] static result<service> _make(const delegated_host* host, config cfg);
    // Replaces the subtypes with a list that is already validated.
    [[nodiscard]] result<void> _set_subtypes(std::vector<std::string> next);
    void _release() noexcept;
    /** @endcond */

    std::unique_ptr<state> _state;
};

/**
 * @headerfile <idfxx/mdns>
 * @brief Checks whether a service is advertised for the device.
 *
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param instance_name Instance name to match, or `std::nullopt` for any
 *        instance of the type.
 *
 * @return `true` if such a service is advertised for the device. `false`
 *         otherwise, and always when the service is not running, no hostname
 *         is set, or the type or instance name is empty, is longer than 63
 *         characters or contains a NUL character.
 */
[[nodiscard]] bool service_exists(
    std::string_view type,
    enum protocol protocol,
    std::optional<std::string_view> instance_name = std::nullopt
);

/**
 * @headerfile <idfxx/mdns>
 * @brief Checks whether a service is advertised on a delegated host.
 *
 * @param host The delegated host.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param instance_name Instance name to match, or `std::nullopt` for any
 *        instance of the type.
 *
 * @return `true` if such a service is advertised on the host. `false`
 *         otherwise, and always when the service is not running, or the type
 *         or instance name is empty, is longer than 63 characters or contains
 *         a NUL character.
 */
[[nodiscard]] bool service_exists(
    const delegated_host& host,
    std::string_view type,
    enum protocol protocol,
    std::optional<std::string_view> instance_name = std::nullopt
);

// =============================================================================
// Local service lookups
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief Tag type selecting services advertised for the device.
 */
struct self_hosted_t {
    /** @brief Constructs the tag. */
    explicit self_hosted_t() = default;
};

/**
 * @headerfile <idfxx/mdns>
 * @brief Tag selecting services advertised for the device.
 */
inline constexpr self_hosted_t self_hosted{};

/**
 * @headerfile <idfxx/mdns>
 * @brief Tag type selecting services advertised on delegated hosts.
 */
struct delegated_t {
    /** @brief Constructs the tag. */
    explicit delegated_t() = default;
};

/**
 * @headerfile <idfxx/mdns>
 * @brief Tag selecting services advertised on delegated hosts.
 */
inline constexpr delegated_t delegated{};

/**
 * @headerfile <idfxx/mdns>
 * @brief Options for a local service lookup.
 */
struct lookup_options {
    /** @brief Only services with this instance name; `std::nullopt` for any. */
    std::optional<std::string> instance_name = std::nullopt;
    /** @brief Only services with this subtype; `std::nullopt` for any. */
    std::optional<std::string> subtype = std::nullopt;
    /** @brief At most this many results, from 1; `std::nullopt` for all. */
    std::optional<uint8_t> max_results = std::nullopt;

    /** @brief Compares two option sets for equality. */
    [[nodiscard]] bool operator==(const lookup_options&) const = default;
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Lists the services of a type advertised for the device.
 *
 * Reads the local service table. Nothing is sent on the network. Answers
 * carry the instance name, type, protocol, hostname, port and TXT record, but
 * no addresses, interface or address family.
 *
 * @param tag @ref self_hosted.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param options Lookup options.
 *
 * @return The matching services.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if the type, instance name or subtype is
 *           empty, is longer than 63 characters or contains a NUL character,
 *           or `max_results` is zero.
 */
[[nodiscard]] std::vector<answer> lookup_services(
    self_hosted_t tag,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options = {}
);

/**
 * @brief Lists the services of a type advertised on delegated hosts.
 *
 * Reads the local service table. Nothing is sent on the network. Answers
 * carry the instance name, type, protocol, hostname, port, TXT record and the
 * host's addresses, but no interface or address family.
 *
 * @param tag @ref delegated.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param options Lookup options.
 *
 * @return The matching services.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if the type, instance name or subtype is
 *           empty, is longer than 63 characters or contains a NUL character,
 *           or `max_results` is zero.
 */
[[nodiscard]] std::vector<answer> lookup_services(
    delegated_t tag,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options = {}
);
#endif

/**
 * @brief Lists the services of a type advertised for the device.
 *
 * Reads the local service table. Nothing is sent on the network. Answers
 * carry the instance name, type, protocol, hostname, port and TXT record, but
 * no addresses, interface or address family.
 *
 * @param tag @ref self_hosted.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param options Lookup options.
 *
 * @return The matching services, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If the type, instance name or subtype is
 *         empty, is longer than 63 characters or contains a NUL character, or
 *         `max_results` is zero.
 */
[[nodiscard]] result<std::vector<answer>> try_lookup_services(
    self_hosted_t tag,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options = {}
);

/**
 * @brief Lists the services of a type advertised on delegated hosts.
 *
 * Reads the local service table. Nothing is sent on the network. Answers
 * carry the instance name, type, protocol, hostname, port, TXT record and the
 * host's addresses, but no interface or address family.
 *
 * @param tag @ref delegated.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param options Lookup options.
 *
 * @return The matching services, or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If the type, instance name or subtype is
 *         empty, is longer than 63 characters or contains a NUL character, or
 *         `max_results` is zero.
 */
[[nodiscard]] result<std::vector<answer>> try_lookup_services(
    delegated_t tag,
    std::string_view type,
    enum protocol protocol,
    const lookup_options& options = {}
);

// =============================================================================
// Network queries
// =============================================================================

/**
 * @headerfile <idfxx/mdns>
 * @brief Configuration for a network query.
 *
 * Which fields to set depends on the record type:
 * - @ref record_type::ptr: `service_type` and `protocol`, to find instances.
 * - @ref record_type::srv and @ref record_type::txt: `name` (the instance
 *   name), `service_type` and `protocol`.
 * - @ref record_type::a and @ref record_type::aaaa: `name` (the hostname).
 *
 * `service_type` and `protocol` are given together or not at all. A query
 * that lacks a field that its record type needs fails with
 * idfxx::errc::invalid_arg. Other fields are allowed.
 *
 * @code
 * auto printers = idfxx::mdns::query({
 *     .service_type = "_ipp",
 *     .protocol = idfxx::mdns::protocol::tcp,
 *     .record_type = idfxx::mdns::record_type::ptr,
 *     .timeout = 2s,
 * });
 * @endcode
 */
struct query_config {
    /** @brief Instance name or hostname to ask about, without `.local`; 1 to 63 characters. */
    std::optional<std::string> name = std::nullopt;
    /** @brief Service type, e.g. `"_http"`; 1 to 63 characters. */
    std::optional<std::string> service_type = std::nullopt;
    /** @brief Service protocol. */
    std::optional<enum protocol> protocol = std::nullopt;
    /** @brief Record type to ask for. */
    required<enum record_type> record_type;
    /**
     * @brief How long to collect answers; positive, and at most
     *        2147483647 ms (`INT32_MAX`, about 24.8 days).
     *
     * The mDNS library keeps a query's deadline in 32-bit milliseconds since
     * boot, so a query ends early if the device's uptime plus the timeout
     * exceeds 2^32 ms (about 49.7 days).
     */
    std::chrono::milliseconds timeout = std::chrono::seconds(3);
    /**
     * @brief Finish early after this many answers, from 1; `std::nullopt`
     *        collects answers until the timeout.
     */
    std::optional<uint8_t> max_results = std::nullopt;

    /** @brief Compares two configurations for equality. */
    [[nodiscard]] bool operator==(const query_config&) const = default;
};

/** @cond INTERNAL */
namespace detail {
[[nodiscard]] result<std::vector<answer>> query_ptr(
    std::string_view type,
    enum protocol protocol,
    std::chrono::milliseconds timeout,
    std::optional<uint8_t> max_results
);
[[nodiscard]] result<std::optional<answer>> query_srv(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    std::chrono::milliseconds timeout
);
[[nodiscard]] result<std::optional<answer>> query_txt(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    std::chrono::milliseconds timeout
);
#if defined(CONFIG_LWIP_IPV4) || defined(__DOXYGEN__)
[[nodiscard]] result<std::optional<net::ipv4_addr>>
query_a(std::string_view hostname, std::chrono::milliseconds timeout);
#endif
#if defined(CONFIG_LWIP_IPV6) || defined(__DOXYGEN__)
[[nodiscard]] result<std::optional<net::ipv6_addr>>
query_aaaa(std::string_view hostname, std::chrono::milliseconds timeout);
#endif

// Longest timeout a query accepts.
inline constexpr std::chrono::milliseconds max_timeout{INT32_MAX};

// Rounds up to milliseconds. Durations beyond what a query accepts (which the
// rounding could overflow) become just over the maximum. Durations that are
// not positive (NaN included) become zero. The query rejects both.
template<typename Rep, typename Period>
[[nodiscard]] constexpr std::chrono::milliseconds
to_timeout(const std::chrono::duration<Rep, Period>& timeout) {
    using fms = std::chrono::duration<double, std::milli>;
    if (!(fms(timeout) > fms::zero())) {
        return std::chrono::milliseconds(0);
    }
    if (fms(timeout) > fms(max_timeout)) {
        return max_timeout + std::chrono::milliseconds(1);
    }
    return std::chrono::ceil<std::chrono::milliseconds>(timeout);
}
} // namespace detail
/** @endcond */

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Sends a query and collects the answers.
 *
 * Blocks until the timeout expires, or until `max_results` answers arrive.
 * PTR queries ask for multicast answers and other queries for unicast
 * answers. Use @ref query(const query_config&, enum transmission) to choose.
 *
 * @param cfg Query configuration.
 *
 * @return The answers. The list is empty if nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if a field that the record type needs is
 *           missing (see @ref query_config), `service_type` and `protocol`
 *           are not given together, a string is empty, is longer than 63
 *           characters or contains a NUL character, the timeout is not
 *           positive or exceeds 2147483647 ms, or `max_results` is zero.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
[[nodiscard]] std::vector<answer> query(const query_config& cfg);

/**
 * @brief Sends a query asking for answers by a chosen transmission, and
 *        collects the answers.
 *
 * Blocks until the timeout expires, or until `max_results` answers arrive.
 *
 * @param cfg Query configuration.
 * @param t How to ask for answers: unicast to the device, or multicast to
 *        the group.
 *
 * @return The answers. The list is empty if nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if a field that the record type needs is
 *           missing (see @ref query_config), `service_type` and `protocol`
 *           are not given together, a string is empty, is longer than 63
 *           characters or contains a NUL character, the timeout is not
 *           positive or exceeds 2147483647 ms, or `max_results` is zero.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 *
 * @code
 * auto hosts = idfxx::mdns::query(
 *     {.name = "printer", .record_type = idfxx::mdns::record_type::a},
 *     idfxx::mdns::transmission::multicast
 * );
 * @endcode
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
[[nodiscard]] std::vector<answer> query(const query_config& cfg, enum transmission t);

/**
 * @brief Starts a query that collects answers in the background.
 *
 * The future completes when the timeout expires, or when `max_results`
 * answers arrive. Dropping every copy of the future before then is allowed:
 * the query runs to its end and is then cleaned up in the background.
 *
 * PTR queries ask for multicast answers and other queries for unicast
 * answers. The transmission cannot be chosen.
 *
 * @param cfg Query configuration.
 *
 * @return A future for the answers. The list of answers is empty if
 *         nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @note Do not wait on the future from an esp_timer callback: the mDNS
 *       library ends queries from the esp_timer task, so the wait would never
 *       return.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if a field that the record type needs is
 *           missing (see @ref query_config), `service_type` and `protocol`
 *           are not given together, a string is empty, is longer than 63
 *           characters or contains a NUL character, the timeout is not
 *           positive or exceeds 2147483647 ms, or `max_results` is zero.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 *
 * @code
 * auto pending = idfxx::mdns::start_query({
 *     .service_type = "_http",
 *     .protocol = idfxx::mdns::protocol::tcp,
 *     .record_type = idfxx::mdns::record_type::ptr,
 * });
 * // ... other work ...
 * for (const auto& a : pending.wait()) { ... }
 * @endcode
 */
[[nodiscard]] idfxx::future<std::vector<answer>> start_query(const query_config& cfg);

/**
 * @brief Finds the instances of a service type.
 *
 * Sends a multicast PTR query and blocks until the timeout expires, or until
 * `max_results` answers arrive.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param timeout How long to collect answers; positive, and at most
 *        2147483647 ms (about 24.8 days).
 * @param max_results Finish early after this many answers, from 1;
 *        `std::nullopt` collects answers until the timeout.
 *
 * @return The answers. The list is empty if nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if the type is empty, is longer than 63
 *           characters or contains a NUL character, the timeout is not
 *           positive (NaN included) or exceeds 2147483647 ms, or
 *           `max_results` is zero.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] std::vector<answer> query_ptr(
    std::string_view type,
    enum protocol protocol,
    const std::chrono::duration<Rep, Period>& timeout,
    std::optional<uint8_t> max_results = std::nullopt
) {
    return unwrap(detail::query_ptr(type, protocol, detail::to_timeout(timeout), max_results));
}

/**
 * @brief Finds the hostname and port of a service instance.
 *
 * Sends an SRV query and blocks until an answer arrives or the timeout
 * expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param instance_name The instance name.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The answer, or `std::nullopt` if nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if a string is empty, is longer than 63
 *           characters or contains a NUL character, or the timeout is not
 *           positive (NaN included) or exceeds 2147483647 ms.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] std::optional<answer> query_srv(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    const std::chrono::duration<Rep, Period>& timeout
) {
    return unwrap(detail::query_srv(instance_name, type, protocol, detail::to_timeout(timeout)));
}

/**
 * @brief Finds the TXT record of a service instance.
 *
 * Sends a TXT query and blocks until an answer arrives or the timeout
 * expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param instance_name The instance name.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The answer, or `std::nullopt` if nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if a string is empty, is longer than 63
 *           characters or contains a NUL character, or the timeout is not
 *           positive (NaN included) or exceeds 2147483647 ms.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] std::optional<answer> query_txt(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    const std::chrono::duration<Rep, Period>& timeout
) {
    return unwrap(detail::query_txt(instance_name, type, protocol, detail::to_timeout(timeout)));
}

#if defined(CONFIG_LWIP_IPV4) || defined(__DOXYGEN__)
/**
 * @brief Resolves a hostname to an IPv4 address.
 *
 * Sends an A query and blocks until an answer arrives or the timeout expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param hostname The hostname, without `.local`.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The address, or `std::nullopt` if nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS and CONFIG_LWIP_IPV4 are enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if the hostname is empty, is longer than 63
 *           characters or contains a NUL character, or the timeout is not
 *           positive (NaN included) or exceeds 2147483647 ms.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] std::optional<net::ipv4_addr>
query_a(std::string_view hostname, const std::chrono::duration<Rep, Period>& timeout) {
    return unwrap(detail::query_a(hostname, detail::to_timeout(timeout)));
}
#endif

#if defined(CONFIG_LWIP_IPV6) || defined(__DOXYGEN__)
/**
 * @brief Resolves a hostname to an IPv6 address.
 *
 * Sends an AAAA query and blocks until an answer arrives or the timeout
 * expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param hostname The hostname, without `.local`.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The address, or `std::nullopt` if nothing answered.
 *
 * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS and CONFIG_LWIP_IPV6 are enabled.
 * @throws std::system_error on failure, with:
 *         - idfxx::errc::invalid_state if the service is not running.
 *         - idfxx::errc::invalid_arg if the hostname is empty, is longer than 63
 *           characters or contains a NUL character, or the timeout is not
 *           positive (NaN included) or exceeds 2147483647 ms.
 *         - idfxx::mdns::errc::limit_reached if the mDNS task's action queue is
 *           full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] std::optional<net::ipv6_addr>
query_aaaa(std::string_view hostname, const std::chrono::duration<Rep, Period>& timeout) {
    return unwrap(detail::query_aaaa(hostname, detail::to_timeout(timeout)));
}
#endif
#endif

/**
 * @brief Sends a query and collects the answers.
 *
 * Blocks until the timeout expires, or until `max_results` answers arrive.
 * PTR queries ask for multicast answers and other queries for unicast
 * answers. Use @ref try_query(const query_config&, enum transmission) to
 * choose.
 *
 * @param cfg Query configuration.
 *
 * @return The answers (empty if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If any of the following is true:
 *         - A field that the record type needs is missing (see
 *           @ref query_config).
 *         - `service_type` and `protocol` are not given together.
 *         - A string is empty, is longer than 63 characters or contains a
 *           NUL character.
 *         - The timeout is not positive or exceeds 2147483647 ms.
 *         - `max_results` is zero.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
[[nodiscard]] result<std::vector<answer>> try_query(const query_config& cfg);

/**
 * @brief Sends a query asking for answers by a chosen transmission, and
 *        collects the answers.
 *
 * Blocks until the timeout expires, or until `max_results` answers arrive.
 *
 * @param cfg Query configuration.
 * @param t How to ask for answers: unicast to the device, or multicast to
 *        the group.
 *
 * @return The answers (empty if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If any of the following is true:
 *         - A field that the record type needs is missing (see
 *           @ref query_config).
 *         - `service_type` and `protocol` are not given together.
 *         - A string is empty, is longer than 63 characters or contains a
 *           NUL character.
 *         - The timeout is not positive or exceeds 2147483647 ms.
 *         - `max_results` is zero.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
[[nodiscard]] result<std::vector<answer>> try_query(const query_config& cfg, enum transmission t);

/**
 * @brief Starts a query that collects answers in the background.
 *
 * The future completes when the timeout expires, or when `max_results`
 * answers arrive. Dropping every copy of the future before then is allowed:
 * the query runs to its end and is then cleaned up in the background.
 *
 * PTR queries ask for multicast answers and other queries for unicast
 * answers. The transmission cannot be chosen.
 *
 * @param cfg Query configuration.
 *
 * @return A future for the answers (empty if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If any of the following is true:
 *         - A field that the record type needs is missing (see
 *           @ref query_config).
 *         - `service_type` and `protocol` are not given together.
 *         - A string is empty, is longer than 63 characters or contains a
 *           NUL character.
 *         - The timeout is not positive or exceeds 2147483647 ms.
 *         - `max_results` is zero.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 *
 * @note Do not wait on the future from an esp_timer callback: the mDNS
 *       library ends queries from the esp_timer task, so the wait would never
 *       return.
 */
[[nodiscard]] result<idfxx::future<std::vector<answer>>> try_start_query(const query_config& cfg);

/**
 * @brief Finds the instances of a service type.
 *
 * Sends a multicast PTR query and blocks until the timeout expires, or until
 * `max_results` answers arrive.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param timeout How long to collect answers; positive, and at most
 *        2147483647 ms (about 24.8 days).
 * @param max_results Finish early after this many answers, from 1;
 *        `std::nullopt` collects answers until the timeout.
 *
 * @return The answers (empty if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If the type is empty, is longer than 63
 *         characters or contains a NUL character, the timeout is not positive
 *         (NaN included) or exceeds 2147483647 ms, or `max_results` is zero.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] result<std::vector<answer>> try_query_ptr(
    std::string_view type,
    enum protocol protocol,
    const std::chrono::duration<Rep, Period>& timeout,
    std::optional<uint8_t> max_results = std::nullopt
) {
    return detail::query_ptr(type, protocol, detail::to_timeout(timeout), max_results);
}

/**
 * @brief Finds the hostname and port of a service instance.
 *
 * Sends an SRV query and blocks until an answer arrives or the timeout
 * expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param instance_name The instance name.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The answer (`std::nullopt` if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If a string is empty, is longer than 63
 *         characters or contains a NUL character, or the timeout is not
 *         positive (NaN included) or exceeds 2147483647 ms.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] result<std::optional<answer>> try_query_srv(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    const std::chrono::duration<Rep, Period>& timeout
) {
    return detail::query_srv(instance_name, type, protocol, detail::to_timeout(timeout));
}

/**
 * @brief Finds the TXT record of a service instance.
 *
 * Sends a TXT query and blocks until an answer arrives or the timeout
 * expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param instance_name The instance name.
 * @param type Service type, e.g. `"_http"`.
 * @param protocol Service protocol.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The answer (`std::nullopt` if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If a string is empty, is longer than 63
 *         characters or contains a NUL character, or the timeout is not
 *         positive (NaN included) or exceeds 2147483647 ms.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] result<std::optional<answer>> try_query_txt(
    std::string_view instance_name,
    std::string_view type,
    enum protocol protocol,
    const std::chrono::duration<Rep, Period>& timeout
) {
    return detail::query_txt(instance_name, type, protocol, detail::to_timeout(timeout));
}

#if defined(CONFIG_LWIP_IPV4) || defined(__DOXYGEN__)
/**
 * @brief Resolves a hostname to an IPv4 address.
 *
 * Sends an A query and blocks until an answer arrives or the timeout expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param hostname The hostname, without `.local`.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The address (`std::nullopt` if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If the hostname is empty, is longer than
 *         63 characters or contains a NUL character, or the timeout is not
 *         positive (NaN included) or exceeds 2147483647 ms.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 *
 * @note Only available when CONFIG_LWIP_IPV4 is enabled.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] result<std::optional<net::ipv4_addr>>
try_query_a(std::string_view hostname, const std::chrono::duration<Rep, Period>& timeout) {
    return detail::query_a(hostname, detail::to_timeout(timeout));
}
#endif

#if defined(CONFIG_LWIP_IPV6) || defined(__DOXYGEN__)
/**
 * @brief Resolves a hostname to an IPv6 address.
 *
 * Sends an AAAA query and blocks until an answer arrives or the timeout
 * expires.
 *
 * @tparam Rep Duration arithmetic type.
 * @tparam Period Duration period type.
 * @param hostname The hostname, without `.local`.
 * @param timeout How long to wait; positive, and at most 2147483647 ms
 *        (about 24.8 days).
 *
 * @return The address (`std::nullopt` if nothing answered), or an error.
 * @retval idfxx::errc::invalid_state If the service is not running.
 * @retval idfxx::errc::invalid_arg If the hostname is empty, is longer than
 *         63 characters or contains a NUL character, or the timeout is not
 *         positive (NaN included) or exceeds 2147483647 ms.
 * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action queue
 *         is full. Retrying later can succeed.
 *
 * @note Only available when CONFIG_LWIP_IPV6 is enabled.
 * @note Must not be called from an esp_timer callback: the mDNS library ends
 *       queries from the esp_timer task, so the call would never return.
 */
template<typename Rep, typename Period>
[[nodiscard]] result<std::optional<net::ipv6_addr>>
try_query_aaaa(std::string_view hostname, const std::chrono::duration<Rep, Period>& timeout) {
    return detail::query_aaaa(hostname, detail::to_timeout(timeout));
}
#endif

// =============================================================================
// Browsing
// =============================================================================

#if defined(CONFIG_MDNS_ENABLE_BROWSE) || defined(__DOXYGEN__)
/**
 * @headerfile <idfxx/mdns>
 * @brief Follows the instances of a service type on the network.
 *
 * While the browser exists, the callback is called with an @ref answer for
 * each instance of the service type: once for each instance already cached
 * when the browser starts, and again whenever an instance appears, changes or
 * goes away (reported with a TTL of zero).
 *
 * The callback runs on the mDNS task while it holds the mDNS lock, so it must
 * return quickly and must not call any idfxx::mdns function, including
 * creating or destroying a browser or dropping a query future. Pass the
 * answer to another task (for example through a queue or an event) for any
 * further work. Exceptions thrown by the callback are logged and ignored.
 *
 * Only one browser may exist at a time for each service type, protocol and
 * subtype.
 *
 * The callback can receive an answer for one of the device's own services
 * with no hostname and no port, when another host queries for the service
 * type. Use @ref lookup_services with idfxx::mdns::self_hosted to read the
 * device's own services.
 *
 * This type is non-copyable and move-only. A moved-from object must not be
 * used: any operation other than destruction or assignment is undefined
 * behavior.
 *
 * @code
 * idfxx::mdns::browser peers({.type = "_http", .protocol = idfxx::mdns::protocol::tcp},
 *     [](const idfxx::mdns::answer& a) {
 *         // a.instance_name, a.hostname, a.port, a.ttl == 0s when it went away
 *     });
 * @endcode
 *
 * @note Only available when CONFIG_MDNS_ENABLE_BROWSE is enabled.
 */
class browser {
public:
    /**
     * @headerfile <idfxx/mdns>
     * @brief Configuration for a browser.
     */
    struct config {
        /** @brief Service type, e.g. `"_http"`; 1 to 63 characters. */
        required<std::string> type;
        /** @brief Service protocol. */
        required<enum protocol> protocol;
        /**
         * @brief Only instances with this subtype, e.g. `"_printer"`; 1 to 63
         *        characters. `std::nullopt` follows every instance.
         */
        std::optional<std::string> subtype = std::nullopt;

        /** @brief Compares two configurations for equality. */
        [[nodiscard]] bool operator==(const config&) const = default;
    };

    /**
     * @brief Callback receiving browse answers.
     *
     * Runs on the mDNS task under the mDNS lock. See the class description.
     */
    using callback = std::move_only_function<void(const answer&) const>;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Starts a browser.
     *
     * @param cfg Browser configuration.
     * @param cb Callback for answers. Must not be empty.
     *
     * @note Only available when CONFIG_COMPILER_CXX_EXCEPTIONS is enabled.
     * @throws std::system_error on failure, with:
     *         - idfxx::errc::invalid_state if the service is not running, or a
     *           browser with the same type, protocol and subtype exists.
     *         - idfxx::errc::invalid_arg if the type or subtype is empty, is
     *           longer than 63 characters or contains a NUL character, or the
     *           callback is empty.
     *         - idfxx::mdns::errc::limit_reached if the mDNS task's action
     *           queue is full. Retrying later can succeed.
     */
    [[nodiscard]] explicit browser(config cfg, callback cb);
#endif

    /**
     * @brief Starts a browser.
     *
     * @param cfg Browser configuration.
     * @param cb Callback for answers. Must not be empty.
     *
     * @return The browser, or an error.
     * @retval idfxx::errc::invalid_state If the service is not running, or a
     *         browser with the same type, protocol and subtype exists.
     * @retval idfxx::errc::invalid_arg If the type or subtype is empty, is
     *         longer than 63 characters or contains a NUL character, or the
     *         callback is empty.
     * @retval idfxx::mdns::errc::limit_reached If the mDNS task's action
     *         queue is full. Retrying later can succeed.
     */
    [[nodiscard]] static result<browser> make(config cfg, callback cb);

    /** @brief Stops the browser. No callback runs after the destructor returns. */
    ~browser();

    browser(const browser&) = delete;
    browser& operator=(const browser&) = delete;

    /**
     * @brief Move constructor.
     *
     * @param other The browser to move from.
     */
    browser(browser&& other) noexcept;

    /**
     * @brief Move assignment. Stops the browser this object held first.
     *
     * @param other The browser to move from.
     * @return Reference to this.
     */
    browser& operator=(browser&& other) noexcept;

private:
    /** @cond INTERNAL */
    struct state;
    explicit browser(std::unique_ptr<state> s) noexcept;
    void _release() noexcept;
    /** @endcond */

    std::unique_ptr<state> _state;
};
#endif

} // namespace idfxx::mdns

namespace idfxx {

/**
 * @headerfile <idfxx/mdns>
 * @brief Returns the DNS-SD label of a service protocol.
 *
 * @param p The protocol.
 * @return "_tcp", "_udp", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(mdns::protocol p);

/**
 * @headerfile <idfxx/mdns>
 * @brief Returns the name of a DNS record type.
 *
 * @param t The record type.
 * @return "A", "PTR", "TXT", "AAAA", "SRV", "ANY", or "unknown(N)" for
 *         unrecognized values.
 */
[[nodiscard]] std::string to_string(mdns::record_type t);

} // namespace idfxx

/** @cond INTERNAL */
namespace std {
template<>
struct is_error_code_enum<idfxx::mdns::errc> : true_type {};
} // namespace std
/** @endcond */

#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::mdns::protocol> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::mdns::protocol p, FormatContext& ctx) const {
        auto s = idfxx::to_string(p);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::mdns::record_type> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::mdns::record_type t, FormatContext& ctx) const {
        auto s = idfxx::to_string(t);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_mdns
