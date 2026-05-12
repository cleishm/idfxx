// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/// @cond INTERNAL

// Internal base class shared by `datagram_socket` and `raw_socket`. Adds the
// per-message send/receive and disconnect operations that connectionless
// sockets share. Stream sockets use the `ip_io_socket_base` API directly.

#include "sdkconfig.h"

#include <idfxx/net/detail/ip_io_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <cstddef>
#include <span>

namespace idfxx::net::detail {

class connectionless_socket_base : public ip_io_socket_base {
public:
    /// @brief Alias for `idfxx::net::datagram`, returned by `recv_from` / `try_recv_from`.
    ///        Inherited by `datagram_socket` and `raw_socket`.
    using datagram = ::idfxx::net::datagram;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a single datagram into the given buffer (connected mode).
     *
     * Each call returns exactly one datagram. An oversized datagram is **not**
     * truncated: if it does not fit in @p buf the whole datagram is consumed and
     * `errc::message_too_long` is raised, so size the buffer for the largest
     * expected datagram or use `recv_from`.
     *
     * @param buf Buffer to receive into.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure, including `errc::message_too_long`
     *         when the datagram is larger than @p buf.
     *
     * @return Sub-span of @p buf with the datagram's bytes. An empty span is a
     *         received zero-length datagram — connectionless sockets have no
     *         end-of-stream, so it never signals peer close.
     */
    [[nodiscard]] std::span<std::byte> recv(std::span<std::byte> buf) { return idfxx::unwrap(try_recv(buf)); }
#endif

    /**
     * @brief Receives a single datagram into the given buffer (connected mode).
     *
     * Each call returns exactly one datagram. An oversized datagram is **not**
     * truncated: if it does not fit in @p buf the whole datagram is consumed and
     * `errc::message_too_long` is returned, so size the buffer for the largest
     * expected datagram or use `try_recv_from`.
     *
     * @param buf Buffer to receive into.
     * @return Sub-span of @p buf with the datagram's bytes, or an error
     *         (`errc::message_too_long` when the datagram is larger than @p buf).
     *         An empty span is a received zero-length datagram — connectionless
     *         sockets have no end-of-stream, so it never signals peer close.
     */
    [[nodiscard]] result<std::span<std::byte>> try_recv(std::span<std::byte> buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Clears the default peer set by `connect`.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void disconnect() { idfxx::unwrap(try_disconnect()); }
#endif

    /**
     * @brief Clears the default peer set by `try_connect`.
     *
     * @return Success, or an error.
     */
    result<void> try_disconnect();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a datagram to a specific peer.
     *
     * @param buf Data to send.
     * @param to  Destination endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The number of bytes sent — always `buf.size()` on success, since a
     *         datagram is sent in full or not at all.
     */
    size_t send_to(std::span<const std::byte> buf, const endpoint& to) { return idfxx::unwrap(try_send_to(buf, to)); }
#endif

    /**
     * @brief Sends a datagram to a specific peer.
     *
     * @param buf Data to send.
     * @param to  Destination endpoint.
     * @return The number of bytes sent — always `buf.size()` on success, since a
     *         datagram is sent in full or not at all — or an error.
     */
    [[nodiscard]] result<size_t> try_send_to(std::span<const std::byte> buf, const endpoint& to);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a datagram and reports the sender's endpoint.
     *
     * @param buf Buffer to fill.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Datagram metadata (filled sub-span, source endpoint).
     */
    [[nodiscard]] datagram recv_from(std::span<std::byte> buf) { return idfxx::unwrap(try_recv_from(buf)); }
#endif

    /**
     * @brief Receives a datagram and reports the sender's endpoint.
     *
     * @param buf Buffer to fill.
     * @return Datagram metadata (filled sub-span, source endpoint), or an error.
     */
    [[nodiscard]] result<datagram> try_recv_from(std::span<std::byte> buf);

protected:
    using ip_io_socket_base::ip_io_socket_base;
};

} // namespace idfxx::net::detail

/// @endcond
