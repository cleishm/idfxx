// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal base class shared by `stream_socket` and `connectionless_socket_base`
// (i.e. by every socket type on which peer-oriented I/O makes sense: stream,
// datagram, raw). Adds `connect`, `peer_endpoint`, `recv`, the send-side
// timeout/buffer options, and `wait_writable` on top of `ip_socket_base`.
//
// `listener` inherits `ip_socket_base` directly and so does not have any of
// these members.

#include "sdkconfig.h"

#include <idfxx/byte_view>
#include <idfxx/net/detail/ip_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstddef>

namespace idfxx::net::detail {

/// @brief Connection, receive and send-side operations common to stream, datagram and raw sockets.
class ip_io_socket_base : public ip_socket_base {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the remote endpoint that this socket is connected to.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error if the socket is not connected or on other failure.
     */
    [[nodiscard]] endpoint peer_endpoint() const { return idfxx::unwrap(try_peer_endpoint()); }
#endif

    /** @brief Returns the remote endpoint, or an error if the socket is not connected. */
    [[nodiscard]] result<endpoint> try_peer_endpoint() const;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Connects the socket to a remote endpoint.
     *
     * For connectionless sockets, sets a default peer for `send` / `recv`.
     *
     * @param peer Remote endpoint to connect to.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void connect(const endpoint& peer) { idfxx::unwrap(try_connect(peer)); }
#endif

    /**
     * @brief Connects the socket to a remote endpoint.
     *
     * @param peer Remote endpoint to connect to.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_connect(const endpoint& peer);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives data into the given buffer.
     *
     * Blocks until data is available, the peer closes the connection, or the
     * receive timeout set by `set_recv_timeout` expires. In non-blocking mode
     * the call returns immediately when no data is available.
     *
     * For an empty @p buf, the call returns an empty view at once without
     * receiving anything. Such a call cannot detect that the peer closed the
     * connection. Pass a non-empty buffer to detect it.
     *
     * @param buf Buffer to receive into.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure, including `errc::timed_out` when
     *         the receive timeout expires and `errc::operation_would_block` when
     *         a non-blocking socket has no data available.
     *
     * @return View of the bytes received, at the start of @p buf. For a
     *         non-empty @p buf, an empty view indicates the peer closed the
     *         connection.
     */
    [[nodiscard]] byte_span recv(byte_span buf) { return idfxx::unwrap(try_recv(buf)); }
#endif

    /**
     * @brief Receives data into the given buffer.
     *
     * Blocks until data is available, the peer closes the connection, or the
     * receive timeout set by `set_recv_timeout` expires. In non-blocking mode
     * the call returns immediately when no data is available.
     *
     * For an empty @p buf, the call returns an empty view at once without
     * receiving anything. Such a call cannot detect that the peer closed the
     * connection. Pass a non-empty buffer to detect it.
     *
     * @param buf Buffer to receive into.
     * @return View of the bytes received, at the start of @p buf, or an error.
     *         For a non-empty @p buf, an empty view indicates the peer closed the
     *         connection.
     * @retval errc::timed_out The receive timeout expired before any data arrived.
     * @retval errc::operation_would_block The socket is non-blocking and no data
     *         is available.
     */
    [[nodiscard]] result<byte_span> try_recv(byte_span buf);

    /**
     * @brief Sets the kernel send buffer size in bytes. Equivalent to the POSIX
     *        `SO_SNDBUF` socket option.
     *
     * @param bytes Buffer size in bytes. Values larger than `INT_MAX` are clamped to
     *              `INT_MAX`. The buffer is allocated up to that limit.
     */
    void set_send_buffer(size_t bytes) noexcept;

    /**
     * @brief Sets the maximum time `send` will block waiting for buffer space. On expiry,
     *        the call fails with `errc::timed_out`. Equivalent to the POSIX `SO_SNDTIMEO`
     *        socket option. Sub-millisecond resolutions are rounded up. Has no effect
     *        in non-blocking mode, where `send` fails immediately with
     *        `errc::operation_would_block` instead of waiting.
     *
     * @param t Send timeout duration.
     */
    template<typename Rep, typename Period>
    void set_send_timeout(const std::chrono::duration<Rep, Period>& t) noexcept {
        _set_send_timeout(std::chrono::ceil<std::chrono::milliseconds>(t));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Waits until the socket is writable or the timeout expires.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error if the timeout expires or on other failure.
     */
    template<typename Rep, typename Period>
    void wait_writable(const std::chrono::duration<Rep, Period>& timeout) {
        idfxx::unwrap(_try_wait_writable(std::chrono::ceil<std::chrono::milliseconds>(timeout)));
    }
#endif

    /**
     * @brief Waits until the socket is writable or the timeout expires.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     * @return Success when writable, `errc::timed_out` when not.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_wait_writable(const std::chrono::duration<Rep, Period>& timeout) {
        return _try_wait_writable(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    /**
     * @brief Returns the kernel send buffer size in bytes. Reads back the POSIX
     *        `SO_SNDBUF` option.
     */
    [[nodiscard]] size_t get_send_buffer() const noexcept;

protected:
    /// @cond INTERNAL
    using ip_socket_base::ip_socket_base;

    void _set_send_timeout(std::chrono::milliseconds t) noexcept;
    [[nodiscard]] result<void> _try_wait_writable(std::chrono::milliseconds timeout);
    /// @endcond
};

} // namespace idfxx::net::detail
