// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/listener>
 * @file listener.hpp
 * @brief TCP server (listening) socket.
 *
 * @defgroup idfxx_net_listener Listener
 * @ingroup idfxx_net
 * @brief A bind+listen socket that produces connected `stream_socket` instances.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/detail/ip_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/stream_socket>

#include <chrono>
#include <optional>
#include <string_view>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/listener>
 * @brief A TCP listening socket.
 *
 * A `listener` binds to a local address, listens, and produces connected
 * `stream_socket` instances on accept. `accept()` may be called repeatedly.
 *
 * Ownership is exclusive: the listener is non-copyable and move-only. A
 * moved-from listener may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `-1`); calling any other method
 * on it is undefined behavior. The destructor closes the listener if open.
 *
 * @code
 * idfxx::net::listener l(8080);
 * while (true) {
 *     auto [client, peer] = l.accept_with_peer();
 *     handle(std::move(client));
 * }
 * @endcode
 */
class [[nodiscard]] listener : public detail::ip_socket_base {
public:
    /**
     * @brief Result of `accept_with_peer` / `try_accept_with_peer` — a newly
     *        accepted connection together with the peer's endpoint.
     *
     * Supports structured bindings:
     * @code
     * auto [client, peer] = l.accept_with_peer();
     * @endcode
     */
    struct accepted_connection {
        stream_socket socket; ///< The connected client socket.
        endpoint peer;        ///< The peer's address and port.
    };

    /** @brief Default depth of the pending-connection queue. */
    static constexpr int default_backlog = 5;

    /**
     * @brief Listener configuration.
     *
     * Defaults produce an IPv4 listening socket with `reuse_address = true`
     * (typical for servers).
     */
    struct config {
        address_family family = address_family::ipv4; ///< Address family.
        int backlog = default_backlog;                ///< Pending connection queue depth.
        /// Allow `bind` to reuse a local address that is still in `TIME_WAIT` after a recent
        /// close — typically required for servers that restart on the same port. Equivalent
        /// to the POSIX `SO_REUSEADDR` socket option.
        bool reuse_address = true;
#ifdef CONFIG_LWIP_IPV6
        /// Restrict an IPv6 listener to IPv6 traffic only — disable acceptance of IPv4-mapped
        /// IPv6 addresses, requiring a separate IPv4 listener for IPv4 clients. Equivalent
        /// to the POSIX `IPV6_V6ONLY` option.
        ///
        /// Only available when `CONFIG_LWIP_IPV6` is enabled.
        bool ipv6_only = false;
#endif
        /// If true, `accept` returns immediately when no connection is pending rather than
        /// blocking the calling task. Equivalent to the POSIX `O_NONBLOCK` flag.
        bool non_blocking = false;
        /// Restrict the listener to a specific network interface, identified by name (e.g.
        /// `"st1"`). Equivalent to the POSIX `SO_BINDTODEVICE` option.
        std::optional<std::string_view> bind_to_device = std::nullopt;
    };

    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates and listens on a TCP listener at the given address.
     *
     * @param bind_addr Local endpoint to bind and listen on.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit listener(const endpoint& bind_addr);

    /**
     * @brief Creates and listens on a TCP listener with the given configuration.
     *
     * Address family in @p cfg is overridden by the family of @p bind_addr.
     *
     * @param bind_addr Local endpoint to bind and listen on.
     * @param cfg       Listener configuration.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit listener(const endpoint& bind_addr, const config& cfg);

    /**
     * @brief Creates a TCP listener bound to all local interfaces on the given port.
     *
     * Picks the most permissive address family available at compile time: when
     * `CONFIG_LWIP_IPV6` is enabled, binds to `ipv6_addr::any()` and (with the
     * default `ipv6_only = false`) accepts both IPv6 and IPv4-mapped clients;
     * otherwise binds to `ipv4_addr::any()`. To force a specific family, use
     * the `(port, config)` overload with `cfg.family`.
     *
     * @param port Local port to listen on.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit listener(port_number port);

    /**
     * @brief Creates a TCP listener bound to all local interfaces on the given port.
     *
     * The address family in @p cfg selects between `ipv4_addr::any()` (default)
     * and `ipv6_addr::any()`.
     *
     * @param port Local port to listen on.
     * @param cfg  Listener configuration.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit listener(port_number port, const config& cfg);
#endif

    /**
     * @brief Creates and listens on a TCP listener at the given address.
     *
     * @param bind_addr Local endpoint to bind and listen on.
     * @return The new listener, or an error.
     */
    [[nodiscard]] static result<listener> make(const endpoint& bind_addr);

    /**
     * @brief Creates and listens on a TCP listener with the given configuration.
     *
     * @param bind_addr Local endpoint to bind and listen on.
     * @param cfg       Listener configuration.
     * @return The new listener, or an error.
     */
    [[nodiscard]] static result<listener> make(const endpoint& bind_addr, const config& cfg);

    /**
     * @brief Creates a TCP listener bound to all local interfaces on the given port.
     *
     * @param port Local port to listen on.
     * @return The new listener, or an error.
     */
    [[nodiscard]] static result<listener> make(port_number port);

    /**
     * @brief Creates a TCP listener bound to all local interfaces on the given port.
     *
     * @param port Local port to listen on.
     * @param cfg  Listener configuration.
     * @return The new listener, or an error.
     */
    [[nodiscard]] static result<listener> make(port_number port, const config& cfg);

    // ----- accept -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Accepts a single incoming connection.
     *
     * Blocks until a client connects.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The connected socket.
     */
    [[nodiscard]] stream_socket accept() { return idfxx::unwrap(try_accept()); }

    /**
     * @brief Accepts a single incoming connection and returns the peer's endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The accepted connection and the peer's endpoint.
     */
    [[nodiscard]] accepted_connection accept_with_peer() { return idfxx::unwrap(try_accept_with_peer()); }

    /**
     * @brief Accepts a single connection within the given timeout.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error if the timeout fires or on other failure.
     *
     * @return The connected socket.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] stream_socket accept_for(const std::chrono::duration<Rep, Period>& timeout) {
        return idfxx::unwrap(_try_accept_for(std::chrono::ceil<std::chrono::milliseconds>(timeout)));
    }
#endif

    /**
     * @brief Accepts a single incoming connection.
     *
     * @return The connected socket, or an error.
     */
    [[nodiscard]] result<stream_socket> try_accept();

    /**
     * @brief Accepts a single incoming connection and returns the peer's endpoint.
     *
     * @return The accepted connection and the peer's endpoint, or an error.
     */
    [[nodiscard]] result<accepted_connection> try_accept_with_peer();

    /**
     * @brief Accepts a single connection within the given timeout.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     * @return The connected socket, or an error such as `errc::timed_out`.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<stream_socket> try_accept_for(const std::chrono::duration<Rep, Period>& timeout) {
        return _try_accept_for(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

private:
    explicit listener(int fd, address_family fam) noexcept
        : ip_socket_base(fd, fam) {}

    [[nodiscard]] result<stream_socket> _try_accept_for(std::chrono::milliseconds timeout);
};

} // namespace idfxx::net

/** @} */ // end of idfxx_net_listener
