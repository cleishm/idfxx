// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/stream_socket>
 * @file stream_socket.hpp
 * @brief Type-safe TCP stream socket.
 *
 * @defgroup idfxx_net_stream_socket Stream Socket
 * @ingroup idfxx_net
 * @brief A reliable byte-stream (TCP) socket.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/detail/ip_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/stream_socket>
 * @brief A TCP stream socket.
 *
 * Use `idfxx::net::listener` to accept inbound connections; this class is for
 * outbound client connections and for the per-connection socket returned by
 * the listener.
 *
 * Ownership is exclusive: the socket is non-copyable and move-only. A
 * moved-from socket reports `is_open() == false` and its `try_*` methods
 * return `errc::invalid_state`. The destructor closes the socket if open.
 *
 * @code
 * idfxx::net::stream_socket sock(idfxx::net::endpoint(idfxx::net::ipv4_addr(127, 0, 0, 1), 8080));
 * sock.set_recv_timeout(std::chrono::seconds(5));
 * std::array<std::byte, 256> buf;
 * auto data = sock.recv(buf);
 * @endcode
 */
class stream_socket : public detail::ip_socket_base {
public:
    /**
     * @brief Stream socket configuration.
     *
     * Defaults produce a blocking IPv4 TCP socket. All fields except `family`
     * correspond directly to options that can also be set later via the
     * `set_*` methods.
     */
    struct config {
        enum family family = family::ipv4; ///< Address family (IPv4 / IPv6).
        /// If true, I/O calls return immediately when no data or buffer space is available
        /// rather than blocking the calling task. Equivalent to the POSIX `O_NONBLOCK` flag.
        bool non_blocking = false;
        /// Maximum time `recv` will block waiting for data before returning `errc::timed_out`.
        std::optional<std::chrono::milliseconds> recv_timeout = std::nullopt;
        /// Maximum time `send` will block waiting for buffer space before returning
        /// `errc::timed_out`.
        std::optional<std::chrono::milliseconds> send_timeout = std::nullopt;
        /// Allow `bind` to reuse a local address that is still in `TIME_WAIT`. Equivalent
        /// to the POSIX `SO_REUSEADDR` socket option.
        bool reuse_address = false;
        /// Disable Nagle's algorithm — small writes go out immediately rather than being
        /// coalesced. Equivalent to the POSIX `TCP_NODELAY` option.
        bool no_delay = false;
        /// Send periodic probes on idle TCP connections to detect dead peers. Equivalent
        /// to the POSIX `SO_KEEPALIVE` option.
        bool keep_alive = false;
        /// Time the connection must be idle before keep-alive probes start. Equivalent to
        /// the POSIX `TCP_KEEPIDLE` option.
        std::optional<std::chrono::seconds> keep_idle = std::nullopt;
        /// Interval between successive keep-alive probes. Equivalent to the POSIX
        /// `TCP_KEEPINTVL` option.
        std::optional<std::chrono::seconds> keep_interval = std::nullopt;
        /// Maximum number of unacknowledged keep-alive probes before the connection is
        /// dropped. Equivalent to the POSIX `TCP_KEEPCNT` option.
        std::optional<uint32_t> keep_count = std::nullopt;
#ifdef CONFIG_LWIP_IPV6
        /// Restrict an IPv6 socket to IPv6 traffic only. Equivalent to the POSIX
        /// `IPV6_V6ONLY` option. Only available when `CONFIG_LWIP_IPV6` is enabled.
        bool ipv6_only = false;
#endif
        /// Restrict the socket to a specific network interface, identified by name.
        /// Equivalent to the POSIX `SO_BINDTODEVICE` option.
        std::optional<std::string_view> bind_to_device = std::nullopt;
    };

    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new TCP socket with default configuration (IPv4, blocking).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] stream_socket();
#endif

    /**
     * @brief Creates a new TCP socket with default configuration (IPv4, blocking).
     *
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<stream_socket> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new TCP socket of the given address family.
     *
     * @param fam Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit stream_socket(enum family fam);
#endif

    /**
     * @brief Creates a new TCP socket of the given address family.
     *
     * @param fam Address family.
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<stream_socket> make(enum family fam);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new TCP socket with the given configuration.
     *
     * @param cfg Socket configuration.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit stream_socket(const config& cfg);
#endif

    /**
     * @brief Creates a new TCP socket with the given configuration.
     *
     * @param cfg Socket configuration.
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<stream_socket> make(const config& cfg);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP socket and synchronously connects to the peer.
     *
     * Address family is taken from the peer endpoint.
     *
     * @param peer Remote endpoint to connect to.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit stream_socket(const endpoint& peer);
#endif

    /**
     * @brief Creates a TCP socket and synchronously connects to the peer.
     *
     * @param peer Remote endpoint to connect to.
     * @return The connected socket, or an error.
     */
    [[nodiscard]] static result<stream_socket> connect_to(const endpoint& peer);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP socket with the given configuration and connects to the peer.
     *
     * Address family is taken from the peer endpoint and overrides `cfg.family`.
     *
     * @param peer Remote endpoint to connect to.
     * @param cfg  Socket configuration.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] stream_socket(const endpoint& peer, const config& cfg);
#endif

    /**
     * @brief Creates a TCP socket with the given configuration and connects to the peer.
     *
     * @param peer Remote endpoint to connect to.
     * @param cfg  Socket configuration.
     * @return The connected socket, or an error.
     */
    [[nodiscard]] static result<stream_socket> connect_to(const endpoint& peer, const config& cfg);

    // ----- connect / disconnect -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Connects the socket with a bounded timeout.
     *
     * @param peer    Remote endpoint to connect to.
     * @param timeout Maximum time to wait for the handshake. Sub-millisecond
     *                resolutions are rounded up.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error if the timeout fires or on other failure.
     */
    template<typename Rep, typename Period>
    void connect_for(const endpoint& peer, const std::chrono::duration<Rep, Period>& timeout) {
        idfxx::unwrap(_try_connect_for(peer, std::chrono::ceil<std::chrono::milliseconds>(timeout)));
    }
#endif

    /**
     * @brief Connects the socket with a bounded timeout.
     *
     * Returns once the connection is established, the timeout elapses, or
     * the connection attempt fails.
     *
     * @param peer    Remote endpoint to connect to.
     * @param timeout Maximum time to wait for the handshake. Sub-millisecond
     *                resolutions are rounded up.
     * @return Success, or an error such as `errc::timed_out`.
     */
    template<typename Rep, typename Period>
    [[nodiscard]] result<void>
    try_connect_for(const endpoint& peer, const std::chrono::duration<Rep, Period>& timeout) {
        return _try_connect_for(peer, std::chrono::ceil<std::chrono::milliseconds>(timeout));
    }

    // ----- send -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends bytes on the connected socket.
     *
     * May send fewer bytes than requested. See `send_all` for a helper that
     * retries until the buffer is fully drained.
     *
     * @param buf Data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Number of bytes sent.
     */
    [[nodiscard]] size_t send(std::span<const std::byte> buf) { return idfxx::unwrap(try_send(buf)); }
#endif

    /**
     * @brief Sends bytes on the connected socket.
     *
     * @param buf Data to send.
     * @return Number of bytes sent, or an error.
     */
    [[nodiscard]] result<size_t> try_send(std::span<const std::byte> buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a string view on the connected socket.
     *
     * @param buf Data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Number of bytes sent.
     */
    [[nodiscard]] size_t send(std::string_view buf) { return idfxx::unwrap(try_send(buf)); }
#endif

    /**
     * @brief Sends a string view on the connected socket.
     *
     * @param buf Data to send.
     * @return Number of bytes sent, or an error.
     */
    [[nodiscard]] result<size_t> try_send(std::string_view buf);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends @p buf in full, retrying on partial sends.
     *
     * @param buf Data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send_all(std::span<const std::byte> buf) { idfxx::unwrap(try_send_all(buf)); }
#endif

    /**
     * @brief Sends @p buf in full, retrying on partial sends.
     *
     * @param buf Data to send.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_all(std::span<const std::byte> buf);

    // ----- receive -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives until @p buf is filled, treating premature close as an error.
     *
     * @param buf Buffer to fill exactly.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error if the peer closes early or on other failure.
     */
    void recv_exact(std::span<std::byte> buf) { idfxx::unwrap(try_recv_exact(buf)); }
#endif

    /**
     * @brief Receives until @p buf is filled, treating premature close as an error.
     *
     * @param buf Buffer to fill exactly.
     * @return Success, or an error (`errc::netconn_closed` if the peer closed early).
     */
    [[nodiscard]] result<void> try_recv_exact(std::span<std::byte> buf);

    // ----- shutdown -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Shuts down both directions on the socket.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void shutdown() { idfxx::unwrap(try_shutdown()); }
#endif

    /**
     * @brief Shuts down both directions on the socket.
     *
     * @return Success, or an error.
     */
    result<void> try_shutdown();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Shuts down one direction on the socket.
     *
     * @param dir Direction to shut down.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void shutdown(direction dir) { idfxx::unwrap(try_shutdown(dir)); }
#endif

    /**
     * @brief Shuts down one direction on the socket.
     *
     * @param dir Direction to shut down.
     * @return Success, or an error.
     */
    result<void> try_shutdown(direction dir);

    // ----- TCP-only options (setters) -----

    /**
     * @brief Toggles whether Nagle's algorithm is disabled — when on, small writes are
     *        sent immediately rather than being coalesced. Equivalent to the POSIX
     *        `TCP_NODELAY` socket option.
     *
     * @param on Whether to disable Nagle's algorithm.
     */
    void set_no_delay(bool on) noexcept;

    /**
     * @brief Toggles TCP keep-alive — periodic probes on idle connections to detect
     *        dead peers. Equivalent to the POSIX `SO_KEEPALIVE` socket option.
     *
     * @param on Whether to enable keep-alive.
     */
    void set_keep_alive(bool on) noexcept;

    /**
     * @brief Sets the time the connection must be idle before keep-alive probes start.
     *        Equivalent to the POSIX `TCP_KEEPIDLE` option. Sub-second resolutions are
     *        rounded up.
     *
     * @param t Idle time.
     */
    template<typename Rep, typename Period>
    void set_keep_idle(const std::chrono::duration<Rep, Period>& t) noexcept {
        _set_keep_idle(std::chrono::ceil<std::chrono::seconds>(t));
    }

    /**
     * @brief Sets the interval between successive keep-alive probes. Equivalent to the
     *        POSIX `TCP_KEEPINTVL` option. Sub-second resolutions are rounded up.
     *
     * @param t Interval between probes.
     */
    template<typename Rep, typename Period>
    void set_keep_interval(const std::chrono::duration<Rep, Period>& t) noexcept {
        _set_keep_interval(std::chrono::ceil<std::chrono::seconds>(t));
    }

    /**
     * @brief Sets the maximum number of unacknowledged keep-alive probes before the
     *        connection is dropped. Equivalent to the POSIX `TCP_KEEPCNT` option.
     *
     * @param count Maximum probe count.
     */
    void set_keep_count(uint32_t count) noexcept;

    // ----- TCP-only options (getters) -----

    /**
     * @brief Returns whether Nagle's algorithm is currently disabled on this socket.
     *        Reads back the POSIX `TCP_NODELAY` option.
     *
     * @warning Calling on a moved-from or closed socket is undefined behavior.
     */
    [[nodiscard]] bool get_no_delay() const noexcept;

    /**
     * @brief Returns whether TCP keep-alive is currently enabled on this socket.
     *        Reads back the POSIX `SO_KEEPALIVE` option.
     *
     * @warning Calling on a moved-from or closed socket is undefined behavior.
     */
    [[nodiscard]] bool get_keep_alive() const noexcept;

    /// @cond INTERNAL
    /**
     * @brief Constructs a stream_socket from an existing descriptor (used by `listener`).
     */
    stream_socket(int fd, enum family fam) noexcept
        : ip_socket_base(fd, fam) {}
    /// @endcond

private:
    [[nodiscard]] result<void> _try_connect_for(const endpoint& peer, std::chrono::milliseconds timeout);
    void _set_keep_idle(std::chrono::seconds s) noexcept;
    void _set_keep_interval(std::chrono::seconds s) noexcept;
};

} // namespace idfxx::net

/** @} */ // end of idfxx_net_stream_socket
