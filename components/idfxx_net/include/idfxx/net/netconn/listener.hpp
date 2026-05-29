// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/listener>
 * @file listener.hpp
 * @brief TCP listener over the netconn API.
 *
 * @defgroup idfxx_net_netconn_listener Netconn Listener
 * @ingroup idfxx_net_netconn
 * @brief A bind+listen netconn that produces connected stream channels.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/detail/base_channel.hpp>
#include <idfxx/net/netconn/stream_channel>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/listener>
 * @brief A TCP listening netconn.
 *
 * Binds to a local address, listens, and produces connected
 * `stream_channel` instances on accept. `accept()` may be called repeatedly.
 *
 * The `make(endpoint)` / `make(port)` factories (and the matching constructors)
 * bind and listen in one step; the lower-level `make()` plus explicit `bind()`
 * / `listen()` remains available when finer control is needed.
 *
 * Ownership is exclusive: the listener is non-copyable and move-only. A
 * moved-from listener may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `nullptr`); calling any other
 * method on it is undefined behavior. The destructor releases the listener if
 * open.
 */
class [[nodiscard]] listener : public detail::base_channel {
public:
    /**
     * @brief Result of `accept_with_peer` / `try_accept_with_peer` — a newly
     *        accepted channel together with the peer's endpoint.
     *
     * Supports structured bindings:
     * @code
     * auto [channel, peer] = srv.accept_with_peer();
     * @endcode
     */
    struct accepted_channel {
        stream_channel channel; ///< The connected channel.
        endpoint peer;          ///< The peer's address and port.
    };

    /** @brief Default depth of the pending-connection queue. */
    static constexpr int default_backlog = 5;

    // ----- construction (unbound) -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an unbound TCP listening netconn with default address family (IPv4).
     *
     * Call `bind()` then `listen()` before accepting.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    listener();
#endif

    /** @brief Creates an unbound TCP listening netconn (IPv4). @return The new listener, or an error. */
    [[nodiscard]] static result<listener> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates an unbound TCP listening netconn for the given address family.
     *
     * @param fam Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit listener(address_family fam);
#endif

    /**
     * @brief Creates an unbound TCP listening netconn for the given address family.
     *
     * @param fam Address family.
     * @return The new listener, or an error.
     */
    [[nodiscard]] static result<listener> make(address_family fam);

    // ----- construction (bind + listen) -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP listening netconn bound to @p bind_addr and listening.
     *
     * Address family is taken from @p bind_addr.
     *
     * @param bind_addr Local endpoint to bind and listen on.
     * @param backlog   Pending connection queue depth.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit listener(const endpoint& bind_addr, int backlog = default_backlog);

    /**
     * @brief Creates a TCP listening netconn bound to all local interfaces on @p port.
     *
     * Picks the most permissive address family available at compile time: IPv6
     * when `CONFIG_LWIP_IPV6` is enabled, otherwise IPv4.
     *
     * @param port    Local port to listen on.
     * @param backlog Pending connection queue depth.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit listener(port_number port, int backlog = default_backlog);
#endif

    /**
     * @brief Creates a TCP listening netconn bound to @p bind_addr and listening.
     *
     * Address family is taken from @p bind_addr.
     *
     * @param bind_addr Local endpoint to bind and listen on.
     * @param backlog   Pending connection queue depth.
     * @return The bound, listening listener, or an error.
     */
    [[nodiscard]] static result<listener> make(const endpoint& bind_addr, int backlog = default_backlog);

    /**
     * @brief Creates a TCP listening netconn bound to all local interfaces on @p port.
     *
     * Picks the most permissive address family available at compile time: IPv6
     * when `CONFIG_LWIP_IPV6` is enabled, otherwise IPv4.
     *
     * @param port    Local port to listen on.
     * @param backlog Pending connection queue depth.
     * @return The bound, listening listener, or an error.
     */
    [[nodiscard]] static result<listener> make(port_number port, int backlog = default_backlog);

    // ----- listen / accept -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Marks the netconn as listening.
     *
     * @param backlog Pending connection queue depth.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void listen(int backlog = default_backlog) { idfxx::unwrap(try_listen(backlog)); }
#endif

    /**
     * @brief Marks the netconn as listening.
     *
     * @param backlog Pending connection queue depth.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_listen(int backlog = default_backlog);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Accepts an incoming TCP connection.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The connected `stream_channel`.
     */
    [[nodiscard]] stream_channel accept() { return idfxx::unwrap(try_accept()); }

    /**
     * @brief Accepts an incoming TCP connection and reports the peer's endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The connected channel and the peer's endpoint.
     */
    [[nodiscard]] accepted_channel accept_with_peer() { return idfxx::unwrap(try_accept_with_peer()); }
#endif

    /**
     * @brief Accepts an incoming TCP connection.
     *
     * @return The connected `stream_channel`, or an error.
     */
    [[nodiscard]] result<stream_channel> try_accept();

    /**
     * @brief Accepts an incoming TCP connection and reports the peer's endpoint.
     *
     * @return The connected channel and the peer's endpoint, or an error.
     */
    [[nodiscard]] result<accepted_channel> try_accept_with_peer();

private:
    listener(::netconn* conn, address_family fam) noexcept
        : base_channel(conn, fam) {}
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_listener
