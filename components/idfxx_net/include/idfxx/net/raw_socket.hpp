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
 * The protocol (e.g. `ip_protocol::icmp`) selects the IP-layer protocol
 * for which the socket is opened. Use `send_to` / `recv_from` for
 * connectionless addressing, or `connect()` to set a default peer.
 *
 * On IPv4 a received packet includes the 20-byte IP header that the stack
 * prepends; the header is **not** part of an outgoing send and is added by the
 * stack. As with `datagram_socket`, a send larger than the link can carry
 * returns `errc::message_too_long`, and a receive into too small a buffer
 * returns `errc::message_too_long` rather than truncating.
 *
 * Ownership is exclusive: the socket is non-copyable and move-only. A
 * moved-from socket may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `-1`); calling any other method
 * on it is undefined behavior. The destructor closes the socket if open.
 */
class [[nodiscard]] raw_socket : public detail::connectionless_socket_base {
public:
    /**
     * @brief Raw socket configuration.
     *
     * `protocol` is required (no sensible default exists). Defaults otherwise
     * produce a blocking IPv4 raw socket.
     */
    struct config {
        ip_protocol protocol = {};                    ///< IP-layer protocol (e.g. `ip_protocol::icmp`). Required.
        address_family family = address_family::ipv4; ///< Address family (IPv4 / IPv6).
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

    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new raw IPv4 socket for the given IP protocol.
     *
     * @param protocol IP-layer protocol (e.g. `ip_protocol::icmp`).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit raw_socket(ip_protocol protocol);
#endif

    /**
     * @brief Creates a new raw IPv4 socket for the given IP protocol.
     *
     * @param protocol IP-layer protocol (e.g. `ip_protocol::icmp`).
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<raw_socket> make(ip_protocol protocol);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new raw socket for the given protocol and address family.
     *
     * @param protocol IP-layer protocol.
     * @param fam      Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    raw_socket(ip_protocol protocol, address_family fam);
#endif

    /**
     * @brief Creates a new raw socket for the given protocol and address family.
     *
     * @param protocol IP-layer protocol.
     * @param fam      Address family.
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<raw_socket> make(ip_protocol protocol, address_family fam);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new raw socket with the given configuration.
     *
     * @param cfg Socket configuration.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit raw_socket(const config& cfg);
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
     *
     * @return The number of bytes sent — always `buf.size()` on success, since a
     *         raw packet is sent in full or not at all.
     */
    size_t send(std::span<const std::byte> buf) { return idfxx::unwrap(try_send(buf)); }
#endif

    /**
     * @brief Sends a raw packet on a connected socket.
     *
     * @param buf Data to send.
     * @return The number of bytes sent — always `buf.size()` on success, since a
     *         raw packet is sent in full or not at all — or an error.
     */
    [[nodiscard]] result<size_t> try_send(std::span<const std::byte> buf);

private:
    struct from_fd_t {
        explicit from_fd_t() = default;
    };

    raw_socket(from_fd_t, int fd, address_family fam) noexcept
        : connectionless_socket_base(fd, fam) {}
};

} // namespace idfxx::net

/** @} */ // end of idfxx_net_raw_socket
