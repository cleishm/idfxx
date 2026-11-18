// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal base class shared by `datagram_socket` and `raw_socket`. Adds the
// per-message send/receive and disconnect operations that connectionless
// sockets share. Stream sockets use the `ip_io_socket_base` API directly.

#include "sdkconfig.h"

#include <idfxx/byte_view>
#include <idfxx/net/detail/ip_io_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <cstddef>

namespace idfxx::net::detail {

/// @brief Per-message send/receive and disconnect operations common to datagram and raw sockets.
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
     *         when the datagram is larger than @p buf, `errc::timed_out` when the
     *         receive timeout set by `set_recv_timeout` expires, and
     *         `errc::operation_would_block` when a non-blocking socket has no
     *         datagram available.
     *
     * @return View of the datagram's bytes, at the start of @p buf. An empty view is a
     *         received zero-length datagram — connectionless sockets have no
     *         end-of-stream, so it never signals peer close.
     */
    [[nodiscard]] byte_span recv(byte_span buf) { return idfxx::unwrap(try_recv(buf)); }
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
     * @return View of the datagram's bytes, at the start of @p buf, or an error. An empty
     *         view is a received zero-length datagram — connectionless sockets
     *         have no end-of-stream, so it never signals peer close.
     * @retval errc::message_too_long The datagram is larger than @p buf.
     * @retval errc::timed_out The receive timeout set by `set_recv_timeout` expired.
     * @retval errc::operation_would_block The socket is non-blocking and no
     *         datagram is available.
     */
    [[nodiscard]] result<byte_span> try_recv(byte_span buf);

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
    size_t send_to(byte_view buf, const endpoint& to) {
        return idfxx::unwrap(try_send_to(buf, to));
    }
#endif

    /**
     * @brief Sends a datagram to a specific peer.
     *
     * @param buf Data to send.
     * @param to  Destination endpoint.
     * @return The number of bytes sent — always `buf.size()` on success, since a
     *         datagram is sent in full or not at all — or an error.
     */
    [[nodiscard]] result<size_t> try_send_to(byte_view buf, const endpoint& to);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a datagram and reports the sender's endpoint.
     *
     * @param buf Buffer to fill.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure, including `errc::timed_out` when the
     *         receive timeout set by `set_recv_timeout` expires and
     *         `errc::operation_would_block` when a non-blocking socket has no
     *         datagram available.
     *
     * @return Datagram metadata (filled view, source endpoint).
     */
    [[nodiscard]] datagram recv_from(byte_span buf) { return idfxx::unwrap(try_recv_from(buf)); }
#endif

    /**
     * @brief Receives a datagram and reports the sender's endpoint.
     *
     * @param buf Buffer to fill.
     * @return Datagram metadata (filled view, source endpoint), or an error.
     * @retval errc::timed_out The receive timeout set by `set_recv_timeout` expired.
     * @retval errc::operation_would_block The socket is non-blocking and no
     *         datagram is available.
     */
    [[nodiscard]] result<datagram> try_recv_from(byte_span buf);

protected:
    /// @cond INTERNAL
    using ip_io_socket_base::ip_io_socket_base;
    /// @endcond
};

} // namespace idfxx::net::detail
