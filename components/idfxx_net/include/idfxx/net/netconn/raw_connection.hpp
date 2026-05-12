// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/raw_connection>
 * @file raw_connection.hpp
 * @brief Raw-protocol netconn connection.
 *
 * @defgroup idfxx_net_netconn_raw_connection Netconn Raw Connection
 * @ingroup idfxx_net_netconn
 * @brief A raw IP connection over the netconn API.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/detail/base_connection.hpp>
#include <idfxx/net/netconn/netbuf>

#include <cstddef>
#include <span>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/raw_connection>
 * @brief A raw IP netconn connection.
 *
 * The IP-layer protocol number (e.g. `IPPROTO_ICMP`) is selected at
 * construction. Use `send_to` / `recv_from` for explicit per-message
 * addressing, or call `connect()` to set a default peer.
 *
 * Ownership is exclusive: the connection is non-copyable and move-only.
 */
class raw_connection : public detail::base_connection {
public:
    /** @brief Alias for `idfxx::net::datagram`, returned by `recv_from` / `try_recv_from`. */
    using datagram = ::idfxx::net::datagram;

    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a raw IPv4 netconn for the given IP protocol.
     *
     * @param protocol IP-layer protocol number (e.g. `IPPROTO_ICMP`).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit raw_connection(int protocol);
#endif

    /**
     * @brief Creates a raw IPv4 netconn for the given IP protocol.
     *
     * @param protocol IP-layer protocol number.
     * @return The new connection, or an error.
     */
    [[nodiscard]] static result<raw_connection> make(int protocol);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a raw netconn for the given IP protocol and address family.
     *
     * @param protocol IP-layer protocol number.
     * @param fam      Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] raw_connection(int protocol, enum family fam);
#endif

    /**
     * @brief Creates a raw netconn for the given IP protocol and address family.
     *
     * @param protocol IP-layer protocol number.
     * @param fam      Address family.
     * @return The new connection, or an error.
     */
    [[nodiscard]] static result<raw_connection> make(int protocol, enum family fam);

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

    // ----- send / receive -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a netbuf on the connected raw netconn.
     *
     * @param buf Netbuf holding the data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send(netbuf& buf) { idfxx::unwrap(try_send(buf)); }
#endif

    /**
     * @brief Sends a netbuf on the connected raw netconn.
     *
     * @param buf Netbuf holding the data to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send(netbuf& buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends raw bytes on the connected raw netconn.
     *
     * @param data Bytes to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send(std::span<const std::byte> data) { idfxx::unwrap(try_send(data)); }
#endif

    /**
     * @brief Sends raw bytes on the connected raw netconn.
     *
     * @param data Bytes to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send(std::span<const std::byte> data);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a netbuf to a specific peer.
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
     * @brief Sends a netbuf to a specific peer.
     *
     * @param buf Netbuf holding the data to send.
     * @param to  Destination endpoint.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_to(netbuf& buf, const endpoint& to);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends raw bytes to a specific peer.
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
     * @brief Sends raw bytes to a specific peer.
     *
     * @param data Bytes to send.
     * @param to   Destination endpoint.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_to(std::span<const std::byte> data, const endpoint& to);

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
     * @brief Receives a packet and reports the sender's endpoint.
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

    /** @brief Receives a packet and reports the sender's endpoint. @see recv_from */
    [[nodiscard]] result<datagram> try_recv_from(std::span<std::byte> buf);

    /// @cond INTERNAL
    /** @brief Constructs a raw_connection taking ownership of a `netconn*`. */
    raw_connection(::netconn* conn, enum family fam) noexcept
        : base_connection(conn, fam) {}
    /// @endcond
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_raw_connection
