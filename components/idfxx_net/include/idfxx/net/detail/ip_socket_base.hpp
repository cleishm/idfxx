// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/// @cond INTERNAL

// Narrow internal base class shared by every public socket type — `listener`,
// `stream_socket`, `datagram_socket`, `raw_socket`. Holds the file descriptor,
// address family, lifecycle (move, close), `bind` / `local_endpoint`,
// `wait_readable`, and the family-generic socket options that are meaningful
// even on a listening (accept-only) socket.
//
// Members that imply peer-oriented I/O (`connect`, `recv`, `peer_endpoint`,
// `set_send_buffer`, `set_send_timeout`, `wait_writable`, `get_send_buffer`)
// live on `ip_io_socket_base`, which sits between this class and the
// connectable socket types — keeping them off `listener` at compile time.
//
// Public inheritance with no virtual functions: there is no polymorphism,
// derived destructors run first, and the base destructor calls `close()` to
// guarantee the descriptor is released even if a derived class forgets.

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace idfxx::net::detail {

class ip_socket_base {
public:
    /** @brief Closes the descriptor if still open. */
    ~ip_socket_base() noexcept;

    ip_socket_base(const ip_socket_base&) = delete;
    ip_socket_base& operator=(const ip_socket_base&) = delete;

    ip_socket_base(ip_socket_base&& other) noexcept;
    ip_socket_base& operator=(ip_socket_base&& other) noexcept;

    /**
     * @brief Returns the underlying socket descriptor, or `-1` if moved-from / closed.
     *
     * For observation and interop only: do not `close()`, free, or change the
     * blocking mode of the returned descriptor — doing so desynchronizes this
     * wrapper's invariants.
     */
    [[nodiscard]] int idf_handle() const noexcept { return _fd; }

    /** @brief Returns the address family this socket was created with. */
    [[nodiscard]] address_family family() const noexcept { return _family; }

    /** @brief Returns true if the socket is open (false after `close()` or move-from). */
    [[nodiscard]] bool is_open() const noexcept { return _fd >= 0; }

    /** @brief Closes the socket. Idempotent. */
    void close() noexcept;

    /**
     * @brief Returns the local endpoint bound to this socket.
     *
     * @return The bound local endpoint, or `std::nullopt` if the socket is
     *         moved-from / closed or the address could not be retrieved.
     */
    [[nodiscard]] std::optional<endpoint> local_endpoint() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Binds the socket to a local endpoint.
     *
     * The address family of @p addr must match the family the socket was
     * created with.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void bind(const endpoint& addr) { idfxx::unwrap(try_bind(addr)); }
#endif

    /**
     * @brief Binds the socket to a local endpoint.
     *
     * The address family of @p addr must match the family the socket was
     * created with.
     *
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_bind(const endpoint& addr);

    /**
     * @brief Toggles non-blocking mode — when on, I/O calls return immediately when no
     *        data or buffer space is available rather than blocking the calling task.
     *        Equivalent to the POSIX `O_NONBLOCK` flag.
     *
     * @param on Whether to enable non-blocking mode.
     */
    void set_non_blocking(bool on) noexcept;

    /**
     * @brief Toggles whether `bind` may reuse a local address still in `TIME_WAIT`.
     *        Typically required for servers that restart on the same port. Equivalent
     *        to the POSIX `SO_REUSEADDR` socket option.
     *
     * @param on Whether to enable address reuse.
     */
    void set_reuse_address(bool on) noexcept;

    /**
     * @brief Sets the kernel receive buffer size in bytes. Equivalent to the POSIX
     *        `SO_RCVBUF` socket option.
     *
     * @param bytes Buffer size in bytes. Effectively capped at `INT_MAX`; larger values
     *              are clamped — the buffer will be allocated up to that cap.
     */
    void set_recv_buffer(size_t bytes) noexcept;

    /**
     * @brief Sets the IP time-to-live (hop limit) for outgoing unicast packets.
     *        Equivalent to the POSIX `IP_TTL` option.
     *
     * @param ttl Time-to-live value.
     */
    void set_ttl(uint8_t ttl) noexcept;

    /**
     * @brief Sets the IP type-of-service / DSCP byte for outgoing packets. Equivalent
     *        to the POSIX `IP_TOS` option. Prefer `set_dscp` for DiffServ marking.
     *
     * @param tos Type-of-service byte.
     */
    void set_tos(uint8_t tos) noexcept;

    /**
     * @brief Sets the Differentiated Services Code Point for outgoing packets.
     *        Equivalent to writing the top 6 bits of the POSIX `IP_TOS` byte; the
     *        bottom 2 bits (ECN) are managed by the stack and are not preserved.
     *
     * @param value DSCP code point (top 6 bits of the traffic-class byte). Custom
     *              values can be passed as `static_cast<dscp>(n)`; only the low 6
     *              bits of @p value are used.
     */
    void set_dscp(dscp value) noexcept;

    /**
     * @brief Sets the maximum time `recv` will block waiting for data; on expiry the
     *        call returns `errc::timed_out`. Equivalent to the POSIX `SO_RCVTIMEO`
     *        socket option. Sub-millisecond resolutions are rounded up.
     *
     *        On a `listener`, this option also bounds the wait performed by
     *        `accept()` — lwIP applies `SO_RCVTIMEO` to both operations on the
     *        same socket.
     *
     * @param t Receive timeout duration.
     */
    template<typename Rep, typename Period>
    void set_recv_timeout(const std::chrono::duration<Rep, Period>& t) noexcept {
        _set_recv_timeout(std::chrono::ceil<std::chrono::milliseconds>(t));
    }

#ifdef CONFIG_LWIP_IPV6
    /**
     * @brief Toggles IPv6-only operation — when on, an IPv6 socket cannot accept or
     *        send IPv4-mapped traffic. Equivalent to the POSIX `IPV6_V6ONLY` option.
     *        The option has no effect on IPv4 sockets, and is typically set before
     *        `bind()`.
     *
     * @param on Whether to enable IPv6-only operation.
     *
     * @note Only available when `CONFIG_LWIP_IPV6` is enabled.
     */
    void set_ipv6_only(bool on) noexcept;
#endif

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Restricts the socket to send and receive on a specific network interface,
     *        identified by name (e.g. `"st1"`). Equivalent to the POSIX `SO_BINDTODEVICE`
     *        option.
     *
     * @param ifname Interface name.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void set_bind_to_device(std::string_view ifname) { idfxx::unwrap(try_set_bind_to_device(ifname)); }
#endif

    /**
     * @brief Restricts the socket to send and receive on a specific network interface,
     *        identified by name (e.g. `"st1"`). Equivalent to the POSIX `SO_BINDTODEVICE`
     *        option.
     *
     * @param ifname Interface name.
     *
     * @return Success, or an error code on failure.
     */
    [[nodiscard]] result<void> try_set_bind_to_device(std::string_view ifname);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Waits until the socket is readable or the timeout fires.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error if the timeout fires or on other failure.
     */
    template<typename Rep, typename Period>
    void wait_readable(const std::chrono::duration<Rep, Period>& timeout) {
        idfxx::unwrap(_try_wait_readable(std::chrono::ceil<std::chrono::milliseconds>(timeout)));
    }
#endif

    /**
     * @brief Waits until the socket is readable or the timeout fires.
     *
     * @param timeout Maximum time to wait. Sub-millisecond resolutions are rounded up.
     * @return Success when readable, `errc::timed_out` when not.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void> try_wait_readable(const std::chrono::duration<Rep, Period>& timeout) {
        return _try_wait_readable(std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    /**
     * @brief Returns the kernel receive buffer size in bytes. Reads back the POSIX
     *        `SO_RCVBUF` option.
     */
    [[nodiscard]] size_t get_recv_buffer() const noexcept;

    /**
     * @brief Returns and clears the pending socket-level error (e.g. an asynchronous
     *        connect failure), mapped to an `idfxx::net` error code. Reads back the
     *        POSIX `SO_ERROR` option.
     *
     * @return The pending error, or an empty `std::error_code` (`{}`, testing
     *         `false`) when there is none.
     */
    [[nodiscard]] std::error_code get_last_error() const noexcept;

    /**
     * @brief Returns and clears the pending socket-level error as the raw platform
     *        `errno` value. Reads back the POSIX `SO_ERROR` option.
     *
     *        Prefer `get_last_error()`, which maps the value into the type-safe
     *        `idfxx::net` error model; this accessor is for callers that need the
     *        unmapped integer (e.g. to forward it to platform APIs).
     *
     * @return The pending `errno` value, or `0` when there is none.
     */
    [[nodiscard]] int idf_last_error() const noexcept;

protected:
    ip_socket_base() noexcept = default;
    ip_socket_base(int fd, address_family fam) noexcept
        : _fd(fd)
        , _family(fam) {}

    void _set_recv_timeout(std::chrono::milliseconds t) noexcept;
    [[nodiscard]] result<void> _try_wait_readable(std::chrono::milliseconds timeout);

    int _fd = -1;
    address_family _family = address_family::ipv4;
};

} // namespace idfxx::net::detail

/// @endcond
