// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/datagram_connection>
 * @file datagram_connection.hpp
 * @brief UDP-only netconn connection.
 *
 * @defgroup idfxx_net_netconn_datagram_connection Netconn Datagram Connection
 * @ingroup idfxx_net_netconn
 * @brief A UDP connection over the netconn API.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/detail/base_connection.hpp>
#include <idfxx/net/netconn/netbuf>

#include <cstddef>
#include <cstdint>
#include <span>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/datagram_connection>
 * @brief UDP transport mode for datagram netconn connections.
 *
 * Most UDP traffic uses `udp` (with checksums). The `udp_lite` and
 * `udp_no_chksum` variants are specialized — only set them if you know your
 * peer expects the corresponding wire format.
 */
enum class udp_mode : int {
    udp,           ///< Standard UDP.
    udp_lite,      ///< UDP-Lite (RFC 3828).
    udp_no_chksum, ///< UDP with checksums disabled.
};

/**
 * @headerfile <idfxx/net/netconn/datagram_connection>
 * @brief A UDP netconn connection.
 *
 * Datagram netconns are connectionless. Use `send_to` / `recv_from` for
 * explicit per-message addressing, or call `connect()` to set a default peer
 * for `send` / `recv`.
 *
 * Ownership is exclusive: the connection is non-copyable and move-only.
 */
class datagram_connection : public detail::base_connection {
public:
    /** @brief Alias for `idfxx::net::datagram`, returned by `recv_from` / `try_recv_from`. */
    using datagram = ::idfxx::net::datagram;

    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a UDP netconn with default address family (IPv4) and standard UDP.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] datagram_connection();
#endif

    /**
     * @brief Creates a UDP netconn with default address family (IPv4) and standard UDP.
     *
     * @return The new connection, or an error.
     */
    [[nodiscard]] static result<datagram_connection> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a UDP netconn for the given address family.
     *
     * @param fam  Address family.
     * @param mode UDP transport mode.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit datagram_connection(enum family fam, enum udp_mode mode = udp_mode::udp);
#endif

    /**
     * @brief Creates a UDP netconn for the given address family.
     *
     * @param fam  Address family.
     * @param mode UDP transport mode.
     * @return The new connection, or an error.
     */
    [[nodiscard]] static result<datagram_connection> make(enum family fam, enum udp_mode mode = udp_mode::udp);

    // ----- connect / disconnect (default peer) -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sets the default peer for `send` / `recv` operations.
     *
     * @param peer Default peer endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void connect(const endpoint& peer) { idfxx::unwrap(try_connect(peer)); }
#endif

    /**
     * @brief Sets the default peer for `send` / `recv`.
     *
     * @param peer Default peer endpoint.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_connect(const endpoint& peer);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Clears the default peer.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void disconnect() { idfxx::unwrap(try_disconnect()); }
#endif

    /**
     * @brief Clears the default peer.
     *
     * @return Success, or an error.
     */
    result<void> try_disconnect();

    // ----- send -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a netbuf on the connected UDP netconn.
     *
     * @param buf Netbuf holding the data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send(netbuf& buf) { idfxx::unwrap(try_send(buf)); }
#endif

    /**
     * @brief Sends a netbuf on the connected UDP netconn.
     *
     * @param buf Netbuf holding the data to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send(netbuf& buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a datagram on the connected UDP netconn.
     *
     * @param data Bytes to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send(std::span<const std::byte> data) { idfxx::unwrap(try_send(data)); }
#endif

    /**
     * @brief Sends a datagram on the connected UDP netconn.
     *
     * @param data Bytes to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send(std::span<const std::byte> data);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a netbuf to a specific UDP peer.
     *
     * @param buf Netbuf holding the data to send.
     * @param to  Destination endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send_to(netbuf& buf, const endpoint& to) { idfxx::unwrap(try_send_to(buf, to)); }
#endif

    /**
     * @brief Sends a netbuf to a specific UDP peer.
     *
     * @param buf Netbuf holding the data to send.
     * @param to  Destination endpoint.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_to(netbuf& buf, const endpoint& to);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a datagram to a specific UDP peer.
     *
     * @param data Bytes to send.
     * @param to   Destination endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send_to(std::span<const std::byte> data, const endpoint& to) { idfxx::unwrap(try_send_to(data, to)); }
#endif

    /**
     * @brief Sends a datagram to a specific UDP peer.
     *
     * @param data Bytes to send.
     * @param to   Destination endpoint.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_to(std::span<const std::byte> data, const endpoint& to);

    // ----- receive -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives the next netbuf from the connection.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The received netbuf.
     */
    [[nodiscard]] netbuf recv() { return idfxx::unwrap(try_recv()); }
#endif

    /**
     * @brief Receives the next netbuf.
     *
     * @return The received netbuf, or an error.
     */
    [[nodiscard]] result<netbuf> try_recv();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives data into a caller-provided buffer.
     *
     * Source-address information is dropped — use `recv_from` if it is needed.
     *
     * @param buf Destination buffer.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Sub-span of @p buf covering the bytes actually received.
     */
    [[nodiscard]] std::span<std::byte> recv(std::span<std::byte> buf) { return idfxx::unwrap(try_recv(buf)); }
#endif

    /**
     * @brief Receives data into a caller-provided buffer.
     *
     * @param buf Destination buffer.
     * @return Sub-span of @p buf covering the bytes actually received, or an error.
     */
    [[nodiscard]] result<std::span<std::byte>> try_recv(std::span<std::byte> buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a UDP datagram together with its source endpoint.
     *
     * @param buf Destination buffer.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Datagram metadata (filled sub-span, source endpoint).
     */
    [[nodiscard]] datagram recv_from(std::span<std::byte> buf) { return idfxx::unwrap(try_recv_from(buf)); }
#endif

    /**
     * @brief Receives a UDP datagram together with its source endpoint.
     *
     * @param buf Destination buffer.
     * @return Datagram metadata (filled sub-span, source endpoint), or an error.
     */
    [[nodiscard]] result<datagram> try_recv_from(std::span<std::byte> buf);

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

    /// @cond INTERNAL
    /** @brief Constructs a datagram_connection taking ownership of a `netconn*`. */
    datagram_connection(::netconn* conn, enum family fam) noexcept
        : base_connection(conn, fam) {}
    /// @endcond
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_datagram_connection
