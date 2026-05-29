// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/raw_channel>
 * @file raw_channel.hpp
 * @brief Raw-protocol netconn channel.
 *
 * @defgroup idfxx_net_netconn_raw_channel Netconn Raw Channel
 * @ingroup idfxx_net_netconn
 * @brief A raw IP channel over the netconn API.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/buffer>
#include <idfxx/net/netconn/detail/connectionless_channel.hpp>

#include <cstddef>
#include <span>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/raw_channel>
 * @brief A raw IP netconn channel.
 *
 * The IP-layer protocol (e.g. `ip_protocol::icmp`) is selected at
 * construction. Use `send_to` / `recv_from` for explicit per-message
 * addressing, or call `connect()` to set a default peer.
 *
 * Ownership is exclusive: the channel is non-copyable and move-only. A
 * moved-from channel may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `nullptr`); calling any other
 * method on it is undefined behavior. The destructor releases the channel if
 * open.
 */
class [[nodiscard]] raw_channel : public detail::connectionless_channel {
public:
    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a raw IPv4 netconn for the given IP protocol.
     *
     * @param protocol IP-layer protocol (e.g. `ip_protocol::icmp`).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit raw_channel(ip_protocol protocol);
#endif

    /**
     * @brief Creates a raw IPv4 netconn for the given IP protocol.
     *
     * @param protocol IP-layer protocol.
     * @return The new channel, or an error.
     */
    [[nodiscard]] static result<raw_channel> make(ip_protocol protocol);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a raw netconn for the given IP protocol and address family.
     *
     * @param protocol IP-layer protocol.
     * @param fam      Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    raw_channel(ip_protocol protocol, address_family fam);
#endif

    /**
     * @brief Creates a raw netconn for the given IP protocol and address family.
     *
     * @param protocol IP-layer protocol.
     * @param fam      Address family.
     * @return The new channel, or an error.
     */
    [[nodiscard]] static result<raw_channel> make(ip_protocol protocol, address_family fam);

private:
    raw_channel(::netconn* conn, address_family fam) noexcept
        : connectionless_channel(conn, fam) {}
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_raw_channel
