// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/// @cond INTERNAL

// Internal base class shared by `stream_socket` and `connectionless_socket_base`
// (i.e. by every socket type on which peer-oriented I/O makes sense: stream,
// datagram, raw). Adds `connect`, `peer_endpoint`, `recv`, the send-side
// timeout/buffer options, and `wait_writable` on top of `ip_socket_base`.
//
// `listener` inherits `ip_socket_base` directly and so does not pick up any of
// these members.

#include "sdkconfig.h"

#include <idfxx/net/detail/ip_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstddef>
#include <span>

namespace idfxx::net::detail {

class ip_io_socket_base : public ip_socket_base {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the remote endpoint this socket is connected to.
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
     * @param buf Buffer to receive into.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Sub-span of @p buf with the bytes actually received. An empty
     *         span on stream sockets indicates the peer closed the connection.
     */
    [[nodiscard]] std::span<std::byte> recv(std::span<std::byte> buf) { return idfxx::unwrap(try_recv(buf)); }
#endif

    /**
     * @brief Receives data into the given buffer.
     *
     * @param buf Buffer to receive into.
     * @return Sub-span of @p buf with the bytes actually received, or an error.
     */
    [[nodiscard]] result<std::span<std::byte>> try_recv(std::span<std::byte> buf);

    /**
     * @brief Sets the kernel send buffer size in bytes. Equivalent to the POSIX
     *        `SO_SNDBUF` socket option.
     *
     * @param bytes Buffer size in bytes. Effectively capped at `INT_MAX`; larger values
     *              are clamped — the buffer will be allocated up to that cap.
     */
    void set_send_buffer(size_t bytes) noexcept;

    /**
     * @brief Sets the maximum time `send` will block waiting for buffer space; on expiry
     *        the call returns `errc::timed_out`. Equivalent to the POSIX `SO_SNDTIMEO`
     *        socket option. Sub-millisecond resolutions are rounded up.
     *
     * @param t Send timeout duration.
     */
    template<typename Rep, typename Period>
    void set_send_timeout(const std::chrono::duration<Rep, Period>& t) noexcept {
        _set_send_timeout(std::chrono::ceil<std::chrono::milliseconds>(t));
    }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Waits until the socket is writable or the timeout fires.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error if the timeout fires or on other failure.
     */
    template<typename Rep, typename Period>
    void wait_writable(const std::chrono::duration<Rep, Period>& timeout) {
        idfxx::unwrap(_try_wait_writable(std::chrono::ceil<std::chrono::milliseconds>(timeout)));
    }
#endif

    /**
     * @brief Waits until the socket is writable or the timeout fires.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     * @return Success when writable, `errc::timed_out` when not.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_wait_writable(const std::chrono::duration<Rep, Period>& timeout) {
        return _try_wait_writable(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    /**
     * @brief Returns the kernel send buffer size in bytes. Reads back the POSIX
     *        `SO_SNDBUF` option.
     */
    [[nodiscard]] size_t get_send_buffer() const noexcept;

protected:
    using ip_socket_base::ip_socket_base;

    void _set_send_timeout(std::chrono::milliseconds t) noexcept;
    [[nodiscard]] result<void> _try_wait_writable(std::chrono::milliseconds timeout);
};

} // namespace idfxx::net::detail

/// @endcond
