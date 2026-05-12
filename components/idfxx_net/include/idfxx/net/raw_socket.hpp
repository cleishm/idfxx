// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/raw_socket>
 * @file raw_socket.hpp
 * @brief Type-safe raw IP socket.
 *
 * @defgroup idfxx_net_raw_socket Raw Socket
 * @ingroup idfxx_net
 * @brief A raw IP socket parameterized by IP-layer protocol number.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/detail/connectionless_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/raw_socket>
 * @brief A raw IP socket.
 *
 * The protocol number (e.g. `IPPROTO_ICMP`) selects the IP-layer protocol
 * for which the socket is opened. Use `send_to` / `recv_from` for
 * connectionless addressing, or `connect()` to set a default peer.
 *
 * Ownership is exclusive: the socket is non-copyable and move-only.
 */
class raw_socket : public detail::connectionless_socket_base {
public:
    /**
     * @brief Raw socket configuration.
     *
     * `protocol` is required (no sensible default exists). Defaults otherwise
     * produce a blocking IPv4 raw socket.
     */
    struct config {
        int protocol = 0;                  ///< IP-layer protocol number (e.g. `IPPROTO_ICMP`).
        enum family family = family::ipv4; ///< Address family (IPv4 / IPv6).
        /// If true, I/O calls return immediately when no data or buffer space is available
        /// rather than blocking the calling task. Equivalent to the POSIX `O_NONBLOCK` flag.
        bool non_blocking = false;
        /// Maximum time `recv`/`recv_from` will block waiting for data before returning
        /// `errc::timed_out`.
        std::optional<std::chrono::milliseconds> recv_timeout = std::nullopt;
        /// Maximum time `send`/`send_to` will block waiting for buffer space before
        /// returning `errc::timed_out`.
        std::optional<std::chrono::milliseconds> send_timeout = std::nullopt;
#ifdef CONFIG_LWIP_IPV6
        /// Restrict an IPv6 socket to IPv6 traffic only. Equivalent to the POSIX
        /// `IPV6_V6ONLY` option. Only available when `CONFIG_LWIP_IPV6` is enabled.
        bool ipv6_only = false;
#endif
        /// Restrict the socket to a specific network interface, identified by name.
        /// Equivalent to the POSIX `SO_BINDTODEVICE` option.
        std::optional<std::string_view> bind_to_device = std::nullopt;
    };

    /** @brief Alias for `idfxx::net::datagram`, returned by `recv_from` / `try_recv_from`. */
    using datagram = ::idfxx::net::datagram;

    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new raw IPv4 socket for the given IP protocol.
     *
     * @param protocol IP-layer protocol number (e.g. `IPPROTO_ICMP`).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit raw_socket(int protocol);
#endif

    /**
     * @brief Creates a new raw IPv4 socket for the given IP protocol.
     *
     * @param protocol IP-layer protocol number (e.g. `IPPROTO_ICMP`).
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<raw_socket> make(int protocol);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new raw socket for the given protocol and address family.
     *
     * @param protocol IP-layer protocol number.
     * @param fam      Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] raw_socket(int protocol, enum family fam);
#endif

    /**
     * @brief Creates a new raw socket for the given protocol and address family.
     *
     * @param protocol IP-layer protocol number.
     * @param fam      Address family.
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<raw_socket> make(int protocol, enum family fam);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new raw socket with the given configuration.
     *
     * @param cfg Socket configuration.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit raw_socket(const config& cfg);
#endif

    /**
     * @brief Creates a new raw socket with the given configuration.
     *
     * @param cfg Socket configuration.
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<raw_socket> make(const config& cfg);

    // ----- I/O -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a raw packet on a connected socket.
     *
     * @param buf Data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send(std::span<const std::byte> buf) { idfxx::unwrap(try_send(buf)); }
#endif

    /** @brief Sends a raw packet on a connected socket. @see send */
    [[nodiscard]] result<void> try_send(std::span<const std::byte> buf);

    /// @cond INTERNAL
    /** @brief Tag type for the internal fd-based constructor. */
    struct from_fd_t {
        explicit from_fd_t() = default;
    };

    /** @brief Constructs a raw_socket from an existing descriptor. */
    raw_socket(from_fd_t, int fd, enum family fam) noexcept
        : connectionless_socket_base(fd, fam) {}
    /// @endcond
};

} // namespace idfxx::net

/** @} */ // end of idfxx_net_raw_socket
