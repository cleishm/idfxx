// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/netif>
 * @file netif.hpp
 * @brief Network interface type definitions and API.
 *
 * @defgroup idfxx_netif Network Interface Component
 * @brief Type-safe network interface management with IP address handling and DHCP support.
 *
 * Provides RAII network interface lifecycle management, IP address value types,
 * DHCP client/server control, DNS configuration, and SNTP time synchronization.
 *
 * Depends on @ref idfxx_core for error handling, @ref idfxx_event for
 * event loop integration, and @ref idfxx_hw_support for MAC addresses.
 * @{
 */

#include <idfxx/chrono>
#include <idfxx/error>
#include <idfxx/event>
#include <idfxx/flags>
#include <idfxx/mac>
#include <idfxx/net>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

typedef struct esp_netif_obj esp_netif_t;

namespace idfxx::netif {

// =============================================================================
// Forward declarations
// =============================================================================

class interface;

// =============================================================================
// Enumerations
// =============================================================================

/**
 * @headerfile <idfxx/netif>
 * @brief DHCP option identifiers.
 */
enum class dhcp_option_id : int {
    // clang-format off
    subnet_mask                   = 1,   /*!< Network mask. */
    domain_name_server            = 6,   /*!< Domain name server. */
    router_solicitation_address   = 32,  /*!< Solicitation router address. */
    requested_ip_address          = 50,  /*!< Request specific IP address. */
    ip_address_lease_time         = 51,  /*!< Request IP address lease time. */
    ip_request_retry_time         = 52,  /*!< Request IP address retry counter. */
    vendor_class_identifier       = 60,  /*!< Vendor Class Identifier of a DHCP client. */
    vendor_specific_info          = 43,  /*!< Vendor Specific Information of a DHCP server. */
    captiveportal_uri             = 114, /*!< Captive Portal URI. */
    // clang-format on
};

/**
 * @headerfile <idfxx/netif>
 * @brief DNS server type.
 */
enum class dns_type : int {
    // clang-format off
    main     = 0, /*!< Main DNS server. */
    backup   = 1, /*!< Backup DNS server. */
    fallback = 2, /*!< Fallback DNS server. */
    // clang-format on
};

/**
 * @headerfile <idfxx/netif>
 * @brief Network interface flags.
 *
 * Can be combined using bitwise operators via the flags<flag> type.
 */
enum class flag : uint32_t {
    // clang-format off
    dhcp_client             = 1u << 0, /*!< DHCP client enabled. */
    dhcp_server             = 1u << 1, /*!< DHCP server enabled. */
    autoup                  = 1u << 2, /*!< Interface comes up automatically. */
    garp                    = 1u << 3, /*!< Send gratuitous ARP on IP change. */
    event_ip_modified       = 1u << 4, /*!< Generate event on IP modification. */
    is_ppp                  = 1u << 5, /*!< Interface is PPP. */
    is_bridge               = 1u << 6, /*!< Interface is a bridge. */
    mldv6_report            = 1u << 7, /*!< Send MLDv6 report on IPv6 events. */
    ipv6_autoconfig_enabled = 1u << 8, /*!< IPv6 autoconfiguration enabled. */
    // clang-format on
};

/**
 * @headerfile <idfxx/netif>
 * @brief IP event IDs.
 *
 * Used with the netif::ip_events event base to register listeners for IP-related events.
 *
 * @code
 * loop.listener_add(idfxx::netif::sta_got_ip4,
 *     [](const idfxx::netif::ip4_event_data& info) {
 *         // use info.ip4.ip, etc.
 *     });
 * @endcode
 */
enum class ip_event_id : int32_t {
    // clang-format off
    sta_got_ip4         = 0, /*!< Station received IP address from AP. */
    sta_lost_ip4        = 1, /*!< Station lost IP address. */
    ap_sta_ip4_assigned = 2, /*!< Soft-AP assigned IP to a connected station. */
    got_ip6            = 3, /*!< Received IPv6 address. */
    eth_got_ip4         = 4, /*!< Ethernet got IP from connected AP. */
    eth_lost_ip4        = 5, /*!< Ethernet lost IP and the IP is reset to 0. */
    ppp_got_ip4         = 6, /*!< PPP interface got IP. */
    ppp_lost_ip4        = 7, /*!< PPP interface lost IP. */
    // clang-format on
};

} // namespace idfxx::netif

/// @cond INTERNAL
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::netif::flag> = true;
/// @endcond

namespace idfxx::netif {

// =============================================================================
// Error handling
// =============================================================================

/**
 * @headerfile <idfxx/netif>
 * @brief Network interface error codes.
 */
enum class errc : esp_err_t {
    // clang-format off
    invalid_params        = 0x5001, /*!< Invalid parameters. */
    if_not_ready          = 0x5002, /*!< Interface not ready. */
    dhcpc_start_failed    = 0x5003, /*!< DHCP client start failed. */
    dhcp_already_started  = 0x5004, /*!< DHCP already started. */
    dhcp_already_stopped  = 0x5005, /*!< DHCP already stopped. */
    no_mem                = 0x5006, /*!< Out of memory. */
    dhcp_not_stopped      = 0x5007, /*!< DHCP not stopped. */
    driver_attach_failed  = 0x5008, /*!< Driver attach failed. */
    init_failed           = 0x5009, /*!< Initialization failed. */
    dns_not_configured    = 0x500A, /*!< DNS not configured. */
    mld6_failed           = 0x500B, /*!< MLD6 operation failed. */
    ip6_addr_failed       = 0x500C, /*!< IPv6 address operation failed. */
    dhcps_start_failed    = 0x500D, /*!< DHCP server start failed. */
    tx_failed             = 0x500E, /*!< Transmit failed. */
    // clang-format on
};

/**
 * @headerfile <idfxx/netif>
 * @brief Error category for network interface errors.
 */
class error_category : public std::error_category {
public:
    /** @brief Returns the name of the error category. */
    [[nodiscard]] const char* name() const noexcept override final;

    /** @brief Returns a human-readable message for the given error code. */
    [[nodiscard]] std::string message(int ec) const override final;
};

} // namespace idfxx::netif

namespace idfxx {

/**
 * @headerfile <idfxx/netif>
 * @brief Returns a reference to the netif error category singleton.
 *
 * @return Reference to the singleton netif::error_category instance.
 */
[[nodiscard]] const netif::error_category& netif_category() noexcept;

/**
 * @headerfile <idfxx/netif>
 * @brief Creates an unexpected error from an ESP-IDF error code, mapping to netif error codes where possible.
 *
 * Converts the ESP-IDF error code to a netif-specific error code if a mapping exists,
 * otherwise falls back to the default IDFXX error category.
 *
 * @param e The ESP-IDF error code.
 * @return An unexpected value suitable for returning from result-returning functions.
 */
[[nodiscard]] std::unexpected<std::error_code> netif_error(esp_err_t e);

} // namespace idfxx

namespace idfxx::netif {

/**
 * @headerfile <idfxx/netif>
 * @brief Creates an error code from an idfxx::netif::errc value.
 *
 * @param e The netif error code enumerator.
 * @return The corresponding std::error_code.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), netif_category()};
}

} // namespace idfxx::netif

/// @cond INTERNAL
namespace std {
template<>
struct is_error_code_enum<idfxx::netif::errc> : std::true_type {};
} // namespace std
/// @endcond

namespace idfxx::netif {

// =============================================================================
// IP events
// =============================================================================

/**
 * @headerfile <idfxx/netif>
 * @brief IP event base.
 *
 * Event base for IP events. Use with the idfxx event loop to register listeners
 * for IP address acquisition, loss, and assignment events.
 */
extern const event_base<ip_event_id> ip_events;

/** @cond INTERNAL */
inline event_base<ip_event_id> idfxx_get_event_base(ip_event_id*) {
    return ip_events;
}
/** @endcond */

// =============================================================================
// Event data wrappers
// =============================================================================

/**
 * @headerfile <idfxx/netif>
 * @brief Information about an acquired IPv4 address.
 *
 * Dispatched with the sta_got_ip4, eth_got_ip4, and ppp_got_ip4 events.
 */
struct ip4_event_data {
    net::ip4_info ip4; /*!< IPv4 address, netmask, and gateway. */
    bool changed;      /*!< Whether the IP address changed from the previous value. */

    /// @cond INTERNAL
    static ip4_event_data from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/netif>
 * @brief Information about an acquired IPv6 address.
 *
 * Dispatched with the got_ip6 event.
 */
struct ip6_event_data {
    net::ip6_addr ip; /*!< IPv6 address. */
    int index;        /*!< IPv6 address index on the interface. */

    /// @cond INTERNAL
    static ip6_event_data from_opaque(const void* event_data);
    /// @endcond
};

/**
 * @headerfile <idfxx/netif>
 * @brief Information about an IP assigned to a station connected to an AP.
 *
 * Dispatched with the ap_sta_ip4_assigned event.
 */
struct ap_sta_ip4_assigned_event_data {
    net::ip4_addr ip; /*!< IPv4 address assigned to the station. */
    mac_address mac;  /*!< MAC address of the connected station. */

    /// @cond INTERNAL
    static ap_sta_ip4_assigned_event_data from_opaque(const void* event_data);
    /// @endcond
};

// =============================================================================
// Typed event constants
// =============================================================================

/** @brief Station received IPv4 address event with IP details. */
inline constexpr idfxx::event<ip_event_id, ip4_event_data> sta_got_ip4{ip_event_id::sta_got_ip4};
/** @brief Station lost IPv4 address event. */
inline constexpr idfxx::event<ip_event_id> sta_lost_ip4{ip_event_id::sta_lost_ip4};
/** @brief Soft-AP assigned IP to a connected station event. */
inline constexpr idfxx::event<ip_event_id, ap_sta_ip4_assigned_event_data> ap_sta_ip4_assigned{
    ip_event_id::ap_sta_ip4_assigned
};
/** @brief Received IPv6 address event. */
inline constexpr idfxx::event<ip_event_id, ip6_event_data> got_ip6{ip_event_id::got_ip6};
/** @brief Ethernet received IPv4 address event with IP details. */
inline constexpr idfxx::event<ip_event_id, ip4_event_data> eth_got_ip4{ip_event_id::eth_got_ip4};
/** @brief Ethernet lost IPv4 address event. */
inline constexpr idfxx::event<ip_event_id> eth_lost_ip4{ip_event_id::eth_lost_ip4};
/** @brief PPP interface received IPv4 address event with IP details. */
inline constexpr idfxx::event<ip_event_id, ip4_event_data> ppp_got_ip4{ip_event_id::ppp_got_ip4};
/** @brief PPP interface lost IPv4 address event. */
inline constexpr idfxx::event<ip_event_id> ppp_lost_ip4{ip_event_id::ppp_lost_ip4};

// =============================================================================
// DNS info
// =============================================================================

/**
 * @headerfile <idfxx/netif>
 * @brief DNS server information.
 *
 * Contains an IPv4 address for a DNS server. Used with get_dns() and set_dns().
 */
struct dns_info {
    net::ip4_addr ip; /*!< IPv4 address of the DNS server. */
};

// =============================================================================
// interface
// =============================================================================

/**
 * @headerfile <idfxx/netif>
 * @brief Network interface handle.
 *
 * Provides access to IP configuration, DHCP, DNS, hostname, and other
 * interface properties.
 *
 * An interface may be *owning* or *non-owning*. Owning interfaces (e.g.
 * from idfxx::wifi::create_default_sta_netif()) stop any running DHCP
 * client/server and release the underlying resource on destruction.
 * Non-owning interfaces (e.g. from get_default() or find_by_key())
 * provide the same API but do not manage the lifetime — the caller
 * must ensure the underlying interface outlives the wrapper.
 *
 * @code
 * auto sta_netif = idfxx::wifi::create_default_sta_netif(); // owning
 * auto hostname = sta_netif.get_hostname();
 * @endcode
 */
class interface {
public:
    /**
     * @brief Constructs an owning interface from an `esp_netif_t` handle.
     *
     * Takes ownership of the handle and will destroy it on destruction.
     * The handle must not be null.
     *
     * @param handle A non-null `esp_netif_t` handle.
     */
    static interface take(esp_netif_t* handle);

    /**
     * @brief Constructs a non-owning interface from an `esp_netif_t` handle.
     *
     * Does not take ownership. The caller must ensure the handle remains
     * valid for the lifetime of this object.
     *
     * @param handle A non-null `esp_netif_t` handle.
     */
    static interface wrap(esp_netif_t* handle);

    ~interface();

    interface(const interface&) = delete;
    interface& operator=(const interface&) = delete;

    /**
     * @brief Move constructs an interface, transferring ownership state.
     *
     * @param other The interface to move from. Left in a moved-from state.
     */
    interface(interface&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr))
        , _owning(other._owning) {}

    /**
     * @brief Move assigns an interface, transferring ownership state.
     *
     * @param other The interface to move from. Left in a moved-from state.
     * @return Reference to this.
     */
    interface& operator=(interface&& other) noexcept;

    /**
     * @brief Returns the underlying ESP-NETIF handle.
     *
     * @return The raw ESP-NETIF handle.
     */
    [[nodiscard]] esp_netif_t* idf_handle() const noexcept { return _handle; }

    // =========================================================================
    // Interface status
    // =========================================================================

    /**
     * @brief Checks if the network interface is up.
     *
     * @return True if the interface is up.
     */
    [[nodiscard]] bool is_up() const;

    /**
     * @brief Returns the interface key string.
     *
     * @return The interface key, or nullptr if not set.
     */
    [[nodiscard]] const char* key() const;

    /**
     * @brief Returns the interface description string.
     *
     * @return The interface description, or nullptr if not set.
     */
    [[nodiscard]] const char* description() const;

    /**
     * @brief Returns the route priority of this interface.
     *
     * @return The route priority value.
     */
    [[nodiscard]] int get_route_priority() const;

    /**
     * @brief Sets the route priority of this interface.
     *
     * @param priority The route priority value to set.
     */
    void set_route_priority(int priority);

    /**
     * @brief Returns the flags set on this interface.
     *
     * @return The interface flags.
     */
    [[nodiscard]] flags<flag> get_flags() const;

    /**
     * @brief Returns the typed "got IP" event for this interface.
     *
     * The returned event can be used to register a listener that fires when
     * this interface acquires an IPv4 address, regardless of interface type.
     *
     * @return The typed event with ip4_event_data data.
     */
    [[nodiscard]] idfxx::event<ip_event_id, ip4_event_data> got_ip4_event() const;

    /**
     * @brief Returns the typed "lost IP" event for this interface.
     *
     * The returned event can be used to register a listener that fires when
     * this interface loses its IPv4 address, regardless of interface type.
     *
     * @return The typed event (no data).
     */
    [[nodiscard]] idfxx::event<ip_event_id> lost_ip4_event() const;

    // =========================================================================
    // MAC address
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Returns the MAC address. Not supported on PPP interfaces.
     *  @throws std::system_error on failure. */
    [[nodiscard]] mac_address get_mac() const { return unwrap(try_get_mac()); }

    /** @brief Sets the MAC address. Not supported on PPP interfaces.
     *  @throws std::system_error on failure. */
    void set_mac(const mac_address& mac) { unwrap(try_set_mac(mac)); }
#endif

    /**
     * @brief Returns the MAC address of this interface.
     *
     * Not supported on point-to-point (PPP) interfaces.
     *
     * @return Result containing the MAC address, or an error code.
     */
    [[nodiscard]] result<mac_address> try_get_mac() const;

    /**
     * @brief Sets the MAC address of this interface.
     *
     * Not supported on point-to-point (PPP) interfaces.
     *
     * @param mac The MAC address to set.
     * @return Result indicating success or an error code.
     */
    result<void> try_set_mac(const mac_address& mac);

    // =========================================================================
    // Hostname
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Returns the hostname.
     *  @throws std::system_error on failure. */
    [[nodiscard]] std::string get_hostname() const { return unwrap(try_get_hostname()); }

    /** @brief Sets the hostname.
     *  @throws std::system_error on failure. */
    void set_hostname(std::string_view hostname) { unwrap(try_set_hostname(hostname)); }
#endif

    /**
     * @brief Returns the hostname of this interface.
     *
     * @return Result containing the hostname, or an error code.
     */
    [[nodiscard]] result<std::string> try_get_hostname() const;

    /**
     * @brief Sets the hostname of this interface.
     *
     * @param hostname The hostname to set.
     * @return Result indicating success or an error code.
     */
    result<void> try_set_hostname(std::string_view hostname);

    // =========================================================================
    // IPv4
    // =========================================================================

    /**
     * @brief Returns the current IPv4 information of this interface.
     *
     * @return The IP info containing address, netmask, and gateway.
     */
    [[nodiscard]] net::ip4_info get_ip4_info() const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Sets the IPv4 information. Fails if DHCP is running.
     *  @throws std::system_error on failure. */
    void set_ip4_info(const net::ip4_info& info) { unwrap(try_set_ip4_info(info)); }
#endif

    /**
     * @brief Sets the IPv4 information of this interface.
     *
     * Fails if the DHCP client or server is running on this interface.
     *
     * @param info The IP info to set.
     * @return Result indicating success or an error code.
     */
    result<void> try_set_ip4_info(const net::ip4_info& info);

    // =========================================================================
    // IPv6
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Creates an IPv6 link-local address for this interface.
     *  @throws std::system_error on failure. */
    void create_ip6_linklocal() { unwrap(try_create_ip6_linklocal()); }

    /** @brief Returns the IPv6 link-local address.
     *  @throws std::system_error on failure. */
    [[nodiscard]] net::ip6_addr get_ip6_linklocal() const { return unwrap(try_get_ip6_linklocal()); }

    /** @brief Returns the IPv6 global address.
     *  @throws std::system_error on failure. */
    [[nodiscard]] net::ip6_addr get_ip6_global() const { return unwrap(try_get_ip6_global()); }
#endif

    /**
     * @brief Creates an IPv6 link-local address for this interface.
     *
     * @return Result indicating success or an error code.
     */
    result<void> try_create_ip6_linklocal();

    /**
     * @brief Returns the IPv6 link-local address of this interface.
     *
     * @return Result containing the IPv6 link-local address, or an error code.
     */
    [[nodiscard]] result<net::ip6_addr> try_get_ip6_linklocal() const;

    /**
     * @brief Returns the IPv6 global address of this interface.
     *
     * @return Result containing the IPv6 global address, or an error code.
     */
    [[nodiscard]] result<net::ip6_addr> try_get_ip6_global() const;

    /**
     * @brief Returns all IPv6 addresses of this interface.
     *
     * @return A vector of all IPv6 addresses.
     */
    [[nodiscard]] std::vector<net::ip6_addr> get_all_ip6() const;

    /**
     * @brief Returns all preferred IPv6 addresses of this interface.
     *
     * @return A vector of preferred IPv6 addresses.
     */
    [[nodiscard]] std::vector<net::ip6_addr> get_all_preferred_ip6() const;

    // =========================================================================
    // DHCP client
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Starts the DHCP client on this interface.
     *  @throws std::system_error on failure. */
    void dhcp_client_start() { unwrap(try_dhcp_client_start()); }

    /** @brief Stops the DHCP client on this interface.
     *  @throws std::system_error on failure. */
    void dhcp_client_stop() { unwrap(try_dhcp_client_stop()); }
#endif

    /**
     * @brief Starts the DHCP client on this interface.
     *
     * @retval dhcp_already_started if the DHCP client is already running.
     * @return Result indicating success or an error code.
     */
    result<void> try_dhcp_client_start();

    /**
     * @brief Stops the DHCP client on this interface.
     *
     * @retval dhcp_already_stopped if the DHCP client is not running.
     * @return Result indicating success or an error code.
     */
    result<void> try_dhcp_client_stop();

    /**
     * @brief Checks if the DHCP client is running on this interface.
     *
     * @return True if the DHCP client is started.
     */
    [[nodiscard]] bool is_dhcp_client_running() const;

    // =========================================================================
    // DHCP server
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Starts the DHCP server on this interface.
     *  @throws std::system_error on failure. */
    void dhcp_server_start() { unwrap(try_dhcp_server_start()); }

    /** @brief Stops the DHCP server on this interface.
     *  @throws std::system_error on failure. */
    void dhcp_server_stop() { unwrap(try_dhcp_server_stop()); }
#endif

    /**
     * @brief Starts the DHCP server on this interface.
     *
     * @retval dhcps_start_failed if the DHCP server failed to start.
     * @retval dhcp_already_started if the DHCP server is already running.
     * @return Result indicating success or an error code.
     */
    result<void> try_dhcp_server_start();

    /**
     * @brief Stops the DHCP server on this interface.
     *
     * @retval dhcp_already_stopped if the DHCP server is not running.
     * @return Result indicating success or an error code.
     */
    result<void> try_dhcp_server_stop();

    /**
     * @brief Checks if the DHCP server is running on this interface.
     *
     * @return True if the DHCP server is started.
     */
    [[nodiscard]] bool is_dhcp_server_running() const;

    // =========================================================================
    // DNS
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Sets the DNS server information.
     *  @throws std::system_error on failure. */
    void set_dns(dns_type type, const dns_info& info) { unwrap(try_set_dns(type, info)); }

    /** @brief Returns the DNS server information.
     *  @throws std::system_error on failure. */
    [[nodiscard]] dns_info get_dns(dns_type type) const { return unwrap(try_get_dns(type)); }
#endif

    /**
     * @brief Sets the DNS server information for this interface.
     *
     * @param type The DNS server type (main, backup, or fallback).
     * @param info The DNS server information.
     * @return Result indicating success or an error code.
     */
    result<void> try_set_dns(dns_type type, const dns_info& info);

    /**
     * @brief Returns the DNS server information for this interface.
     *
     * @param type The DNS server type to query.
     * @return Result containing the DNS info, or an error code.
     */
    [[nodiscard]] result<dns_info> try_get_dns(dns_type type) const;

    // =========================================================================
    // NAPT
    // =========================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /** @brief Enables or disables NAPT on this interface.
     *  @throws std::system_error on failure. */
    void napt_enable(bool enable = true) { unwrap(try_napt_enable(enable)); }
#endif

    /**
     * @brief Enables or disables NAPT (Network Address Port Translation) on this interface.
     *
     * @param enable True to enable NAPT, false to disable.
     * @return Result indicating success or an error code.
     */
    result<void> try_napt_enable(bool enable = true);

private:
    interface(esp_netif_t* handle, bool owning) noexcept
        : _handle(handle)
        , _owning(owning) {}

    esp_netif_t* _handle = nullptr;
    bool _owning = true;
};

// =============================================================================
// Subsystem lifecycle
// =============================================================================

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/** @brief Initializes the TCP/IP stack and network interface subsystem.
 *  @throws std::system_error on failure. */
void init();

/** @brief Deinitializes the network interface subsystem.
 *  @throws std::system_error on failure. */
void deinit();
#endif

/**
 * @brief Initializes the TCP/IP stack and network interface subsystem.
 *
 * Must be called before creating any network interfaces.
 *
 * @return Result indicating success or an error code.
 */
result<void> try_init();

/**
 * @brief Deinitializes the network interface subsystem.
 *
 * @return Result indicating success or an error code.
 */
result<void> try_deinit();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline void init() {
    unwrap(try_init());
}
inline void deinit() {
    unwrap(try_deinit());
}
#endif

// =============================================================================
// Discovery
// =============================================================================

/**
 * @brief Returns the number of registered network interfaces.
 *
 * @return The number of interfaces.
 */
[[nodiscard]] size_t get_nr_of_ifs();

/**
 * @brief Returns a non-owning handle to the default network interface.
 *
 * @return A non-owning interface, or std::nullopt if no default is set.
 */
[[nodiscard]] std::optional<interface> get_default();

/**
 * @brief Sets the given interface as the default network interface.
 *
 * @param iface The interface to set as default.
 */
void set_default(interface& iface);

/**
 * @brief Finds a network interface by its key string.
 *
 * @param key The interface key to search for.
 * @return A non-owning interface, or std::nullopt if not found.
 */
[[nodiscard]] std::optional<interface> find_by_key(const char* key);

// =============================================================================
// SNTP sub-namespace
// =============================================================================

/**
 * @defgroup idfxx_netif_sntp SNTP Time Synchronization
 * @ingroup idfxx_netif
 * @brief Simple Network Time Protocol client for time synchronization.
 * @{
 */
namespace sntp {

/**
 * @headerfile <idfxx/netif>
 * @brief SNTP client configuration.
 */
struct config {
    bool smooth_sync = false;                              /*!< Use smooth time synchronization. */
    bool server_from_dhcp = false;                         /*!< Request NTP server from DHCP. */
    bool wait_for_sync = true;                             /*!< Create semaphore for sync notification. */
    bool start = true;                                     /*!< Automatically start SNTP service. */
    bool renew_servers_after_new_ip = false;               /*!< Refresh server list on new IP. */
    ip_event_id renew_event_id = ip_event_id::sta_got_ip4; /*!< IP event that triggers server refresh. */
    std::vector<std::string> servers;                      /*!< NTP server hostnames (e.g. "pool.ntp.org"). */
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/** @brief Initializes the SNTP client with the given configuration.
 *  @throws std::system_error on failure. */
void init(const config& cfg);

/** @brief Starts the SNTP client.
 *  @throws std::system_error on failure. */
void start();
#endif

/**
 * @brief Initializes the SNTP client with the given configuration.
 *
 * @param cfg The SNTP configuration.
 * @return Result indicating success or an error code.
 */
result<void> try_init(const config& cfg);

/**
 * @brief Starts the SNTP client.
 *
 * @return Result indicating success or an error code.
 */
result<void> try_start();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
inline void init(const config& cfg) {
    unwrap(try_init(cfg));
}
inline void start() {
    unwrap(try_start());
}
#endif

/**
 * @brief Deinitializes the SNTP client and frees resources.
 */
void deinit();

/** @cond INTERNAL */
namespace detail {
[[nodiscard]] bool sync_wait_ms(std::chrono::milliseconds timeout);
} // namespace detail
/** @endcond */

/**
 * @brief Waits for SNTP time synchronization to complete.
 *
 * @tparam Rep Duration representation type.
 * @tparam Period Duration period type.
 * @param timeout Maximum time to wait for synchronization.
 * @return True if synchronization completed within the timeout, false otherwise.
 */
template<typename Rep, typename Period>
[[nodiscard]] bool sync_wait(const std::chrono::duration<Rep, Period>& timeout) {
    return detail::sync_wait_ms(std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
}

} // namespace sntp
/** @} */ // end of idfxx_netif_sntp

} // namespace idfxx::netif

// =============================================================================
// String conversions (in idfxx namespace)
// =============================================================================

namespace idfxx {

/**
 * @headerfile <idfxx/netif>
 * @brief Returns the string representation of a DNS server type.
 *
 * @param t The DNS type to convert.
 * @return "main", "backup", "fallback", or "unknown(N)".
 */
[[nodiscard]] std::string to_string(netif::dns_type t);

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/// @cond INTERNAL
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::netif::dns_type> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::netif::dns_type t, FormatContext& ctx) const {
        auto str = idfxx::to_string(t);
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};

} // namespace std
/// @endcond
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_netif
