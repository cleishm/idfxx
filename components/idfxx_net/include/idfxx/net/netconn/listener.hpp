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
 * @brief A bind+listen netconn that produces connected stream connections.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/detail/base_connection.hpp>
#include <idfxx/net/netconn/stream_connection>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/listener>
 * @brief A TCP listening netconn.
 *
 * Binds to a local address, listens, and produces connected
 * `stream_connection` instances on accept. Move-only; `accept()` may be
 * called repeatedly.
 */
class listener : public detail::base_connection {
public:
    /** @brief Default depth of the pending-connection queue. */
    static constexpr int default_backlog = 5;

    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP listening netconn with default address family (IPv4).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] listener();
#endif

    /** @brief Creates a TCP listening netconn (IPv4). @return The new listener, or an error. */
    [[nodiscard]] static result<listener> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP listening netconn for the given address family.
     *
     * @param fam Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit listener(enum family fam);
#endif

    /**
     * @brief Creates a TCP listening netconn for the given address family.
     *
     * @param fam Address family.
     * @return The new listener, or an error.
     */
    [[nodiscard]] static result<listener> make(enum family fam);

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
     * @return The connected `stream_connection`.
     */
    [[nodiscard]] stream_connection accept() { return idfxx::unwrap(try_accept()); }
#endif

    /**
     * @brief Accepts an incoming TCP connection.
     *
     * @return The connected `stream_connection`, or an error.
     */
    [[nodiscard]] result<stream_connection> try_accept();

    /// @cond INTERNAL
    /** @brief Constructs a listener taking ownership of a `netconn*`. */
    listener(::netconn* conn, enum family fam) noexcept
        : base_connection(conn, fam) {}
    /// @endcond
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_listener
