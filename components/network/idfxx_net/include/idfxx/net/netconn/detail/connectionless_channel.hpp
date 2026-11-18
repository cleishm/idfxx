// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal base class shared by `datagram_channel` and `raw_channel`, the
// connectionless netconn-API channel types. Provides the connect / disconnect /
// send / recv operations that have the same shape on both — per-message
// addressing, default-peer support, and the buffer and byte-view send and recv overloads.
// Mirrors the BSD-side `connectionless_socket_base`.

#include "base_channel.hpp"
#include "sdkconfig.h"

#include <idfxx/byte_view>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/buffer>

#include <cstddef>

namespace idfxx::net::netconn::detail {

/// @brief Shared implementation of the connect / disconnect / send / recv
///        family for the connectionless netconn channel types.
class connectionless_channel : public base_channel {
public:
    /// @brief Alias for `idfxx::net::datagram`, returned by `recv_from` / `try_recv_from`.
    using datagram = ::idfxx::net::datagram;

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

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a buffer on the connected netconn.
     *
     * @param buf Buffer holding the data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The number of bytes sent — always the buffer's length on success,
     *         since a connectionless packet is sent in full or not at all.
     */
    size_t send(buffer& buf) { return idfxx::unwrap(try_send(buf)); }
#endif

    /**
     * @brief Sends a buffer on the connected netconn.
     *
     * @param buf Buffer holding the data to send.
     * @return The number of bytes sent — always the buffer's length on success,
     *         since a connectionless packet is sent in full or not at all — or
     *         an error.
     */
    [[nodiscard]] result<size_t> try_send(buffer& buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends raw bytes on the connected netconn.
     *
     * @param data Bytes to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The number of bytes sent — always `data.size()` on success, since
     *         a connectionless packet is sent in full or not at all.
     */
    size_t send(byte_view data) { return idfxx::unwrap(try_send(data)); }
#endif

    /**
     * @brief Sends raw bytes on the connected netconn.
     *
     * @param data Bytes to send.
     * @return The number of bytes sent — always `data.size()` on success, since
     *         a connectionless packet is sent in full or not at all — or an
     *         error.
     */
    [[nodiscard]] result<size_t> try_send(byte_view data);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a buffer to a specific peer.
     *
     * @param buf Buffer holding the data to send.
     * @param to  Destination endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The number of bytes sent — always the buffer's length on success,
     *         since a connectionless packet is sent in full or not at all.
     */
    size_t send_to(buffer& buf, const endpoint& to) { return idfxx::unwrap(try_send_to(buf, to)); }
#endif

    /**
     * @brief Sends a buffer to a specific peer.
     *
     * @param buf Buffer holding the data to send.
     * @param to  Destination endpoint.
     * @return The number of bytes sent — always the buffer's length on success,
     *         since a connectionless packet is sent in full or not at all — or
     *         an error.
     */
    [[nodiscard]] result<size_t> try_send_to(buffer& buf, const endpoint& to);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends raw bytes to a specific peer.
     *
     * @param data Bytes to send.
     * @param to   Destination endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The number of bytes sent — always `data.size()` on success, since
     *         a connectionless packet is sent in full or not at all.
     */
    size_t send_to(byte_view data, const endpoint& to) {
        return idfxx::unwrap(try_send_to(data, to));
    }
#endif

    /**
     * @brief Sends raw bytes to a specific peer.
     *
     * @param data Bytes to send.
     * @param to   Destination endpoint.
     * @return The number of bytes sent — always `data.size()` on success, since
     *         a connectionless packet is sent in full or not at all — or an
     *         error.
     */
    [[nodiscard]] result<size_t> try_send_to(byte_view data, const endpoint& to);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives the next buffer from the connection.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The received buffer.
     */
    [[nodiscard]] buffer recv() { return idfxx::unwrap(try_recv()); }
#endif

    /**
     * @brief Receives the next buffer.
     *
     * @return The received buffer, or an error.
     */
    [[nodiscard]] result<buffer> try_recv();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a single packet into a caller-provided buffer.
     *
     * An oversized packet is **not** truncated: if it does not fit in @p buf the
     * whole packet is consumed and `errc::message_too_long` is raised. Size the
     * buffer for the largest expected packet or use `recv_from`.
     *
     * @param buf Destination buffer.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure, including `errc::message_too_long`
     *         when the packet is larger than @p buf.
     *
     * @return View of the packet's bytes, at the start of @p buf. An empty view is a
     *         received zero-length packet — connectionless channels have no
     *         end-of-stream, so it never signals peer close.
     */
    [[nodiscard]] byte_span recv(byte_span buf) { return idfxx::unwrap(try_recv(buf)); }
#endif

    /**
     * @brief Receives a single packet into a caller-provided buffer.
     *
     * An oversized packet is **not** truncated: if it does not fit in @p buf the
     * whole packet is consumed and `errc::message_too_long` is returned. Size the
     * buffer for the largest expected packet or use `try_recv_from`.
     *
     * @param buf Destination buffer.
     * @return View of the packet's bytes, at the start of @p buf, or an error
     *         (`errc::message_too_long` when the packet is larger than @p buf).
     *         An empty view is a received zero-length packet — connectionless
     *         channels have no end-of-stream, so it never signals peer close.
     */
    [[nodiscard]] result<byte_span> try_recv(byte_span buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a packet and reports the sender's endpoint.
     *
     * @param buf Destination buffer.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Datagram metadata (filled view, source endpoint).
     */
    [[nodiscard]] datagram recv_from(byte_span buf) { return idfxx::unwrap(try_recv_from(buf)); }
#endif

    /**
     * @brief Receives a packet and reports the sender's endpoint.
     *
     * @param buf Destination buffer.
     * @return Datagram metadata (filled view, source endpoint), or an error.
     */
    [[nodiscard]] result<datagram> try_recv_from(byte_span buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the default peer endpoint set via `connect`.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure, including `errc::not_connected`
     *         when no default peer has been set.
     *
     * @return The peer's endpoint.
     */
    [[nodiscard]] endpoint peer_endpoint() const { return idfxx::unwrap(try_peer_endpoint()); }
#endif

    /**
     * @brief Returns the default peer endpoint set via `connect`.
     *
     * @return The peer's endpoint, or an error — `errc::not_connected` when no
     *         default peer has been set.
     */
    [[nodiscard]] result<endpoint> try_peer_endpoint() const { return _try_getaddr(false); }

protected:
    /// @cond INTERNAL
    using base_channel::base_channel;
    /// @endcond
};

} // namespace idfxx::net::netconn::detail
