// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/datagram_channel>
 * @file datagram_channel.hpp
 * @brief UDP netconn channel.
 *
 * @defgroup idfxx_net_netconn_datagram_channel Netconn Datagram Channel
 * @ingroup idfxx_net_netconn
 * @brief A UDP channel over the netconn API.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/buffer>
#include <idfxx/net/netconn/detail/connectionless_channel.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/datagram_channel>
 * @brief UDP transport variant for datagram netconn channels.
 *
 * Most UDP traffic uses `standard` (with checksums). The `lite` and
 * `no_checksum` variants are specialized — only set them if you know your
 * peer expects the corresponding wire format.
 */
enum class udp_variant : int {
    standard,    ///< Standard UDP.
    lite,        ///< UDP-Lite (RFC 3828).
    no_checksum, ///< UDP with checksums disabled.
};

/**
 * @headerfile <idfxx/net/netconn/datagram_channel>
 * @brief A UDP netconn channel.
 *
 * UDP is connectionless. Use `send_to` / `recv_from` for explicit per-message
 * addressing, or call `connect()` to set a default peer for `send` / `recv`.
 *
 * Ownership is exclusive: the channel is non-copyable and move-only. A
 * moved-from channel may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `nullptr`); calling any other
 * method on it is undefined behavior. The destructor releases the channel if
 * open.
 */
class [[nodiscard]] datagram_channel : public detail::connectionless_channel {
public:
    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a UDP netconn with default address family (IPv4) and standard UDP.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    datagram_channel();
#endif

    /**
     * @brief Creates a UDP netconn with default address family (IPv4) and standard UDP.
     *
     * @return The new channel, or an error.
     */
    [[nodiscard]] static result<datagram_channel> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a UDP netconn for the given address family.
     *
     * @param fam     Address family.
     * @param variant UDP transport variant.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit datagram_channel(address_family fam, udp_variant variant = udp_variant::standard);
#endif

    /**
     * @brief Creates a UDP netconn for the given address family.
     *
     * @param fam     Address family.
     * @param variant UDP transport variant.
     * @return The new channel, or an error.
     */
    [[nodiscard]] static result<datagram_channel> make(address_family fam, udp_variant variant = udp_variant::standard);

    // ----- multicast -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Joins an IPv4 multicast group.
     *
     * @param group     Multicast group address to join.
     * @param interface Local interface address (`{}` for default).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure (including `errc::wrong_protocol_type` if
     *         the netconn's family is not IPv4).
     */
    void join_multicast_v4(ipv4_addr group, ipv4_addr interface = {}) {
        idfxx::unwrap(try_join_multicast_v4(group, interface));
    }
#endif

    /** @brief Joins an IPv4 multicast group. @see join_multicast_v4 */
    [[nodiscard]] result<void> try_join_multicast_v4(ipv4_addr group, ipv4_addr interface = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Leaves an IPv4 multicast group.
     *
     * @param group     Multicast group address to leave.
     * @param interface Local interface address (`{}` for default).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void leave_multicast_v4(ipv4_addr group, ipv4_addr interface = {}) {
        idfxx::unwrap(try_leave_multicast_v4(group, interface));
    }
#endif

    /** @brief Leaves an IPv4 multicast group. @see leave_multicast_v4 */
    result<void> try_leave_multicast_v4(ipv4_addr group, ipv4_addr interface = {});

private:
    datagram_channel(::netconn* conn, address_family fam) noexcept
        : connectionless_channel(conn, fam) {}
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_datagram_channel
