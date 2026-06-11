// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/datagram_socket>
 * @file datagram_socket.hpp
 * @brief Type-safe UDP datagram socket.
 *
 * @defgroup idfxx_net_datagram_socket Datagram Socket
 * @ingroup idfxx_net
 * @brief A connectionless datagram (UDP) socket.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/detail/connectionless_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/datagram_socket>
 * @brief A UDP datagram socket.
 *
 * Datagram sockets are connectionless. Use `send_to` / `recv_from` for
 * explicit per-message addressing, or call `connect()` to set a default peer
 * for `send` / `recv`.
 *
 * Neither receive path silently truncates: `recv_from` reports an oversized
 * datagram via `datagram::truncated`, while the connected-mode `recv(span)`
 * returns `errc::message_too_long`. Either way the datagram is consumed whole,
 * so size the buffer for the largest expected datagram (e.g. 1500 bytes). On a
 * connectionless socket an empty `recv` span is a received zero-length datagram,
 * never an end-of-stream signal.
 *
 * Ownership is exclusive: the socket is non-copyable and move-only. A
 * moved-from socket may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `-1`); calling any other method
 * on it is undefined behavior. The destructor closes the socket if open.
 *
 * @code
 * idfxx::net::datagram_socket sock(idfxx::net::address_family::ipv4);
 * sock.bind({idfxx::net::ipv4_addr::any(), 5005});
 * std::array<std::byte, 1500> buf;
 * auto dg = sock.recv_from(buf);
 * sock.send_to(dg.data, dg.from);
 * @endcode
 */
class [[nodiscard]] datagram_socket : public detail::connectionless_socket_base {
public:
    /**
     * @brief Datagram socket configuration.
     *
     * Defaults produce a blocking IPv4 UDP socket.
     */
    struct config {
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
        /// Allow `bind` to reuse a local address. Equivalent to the POSIX `SO_REUSEADDR`
        /// socket option.
        bool reuse_address = false;
        /// Permit sending datagrams to broadcast addresses. Equivalent to the POSIX
        /// `SO_BROADCAST` socket option.
        bool broadcast = false;
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
     * @brief Creates a new UDP socket with default configuration (IPv4, blocking).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    datagram_socket();
#endif

    /**
     * @brief Creates a new UDP socket with default configuration (IPv4, blocking).
     *
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<datagram_socket> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new UDP socket of the given address family.
     *
     * @param fam Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit datagram_socket(address_family fam);
#endif

    /**
     * @brief Creates a new UDP socket of the given address family.
     *
     * @param fam Address family.
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<datagram_socket> make(address_family fam);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a new UDP socket with the given configuration.
     *
     * @param cfg Socket configuration.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit datagram_socket(const config& cfg);
#endif

    /**
     * @brief Creates a new UDP socket with the given configuration.
     *
     * @param cfg Socket configuration.
     * @return The new socket, or an error.
     */
    [[nodiscard]] static result<datagram_socket> make(const config& cfg);

    // ----- send -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a datagram on a connected socket (after `connect`).
     *
     * @param buf Data to send.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The number of bytes sent — always `buf.size()` on success, since a
     *         datagram is sent in full or not at all.
     */
    size_t send(std::span<const std::byte> buf) { return idfxx::unwrap(try_send(buf)); }
#endif

    /**
     * @brief Sends a datagram on a connected socket (after `try_connect`).
     *
     * @param buf Data to send.
     * @return The number of bytes sent — always `buf.size()` on success, since a
     *         datagram is sent in full or not at all — or an error.
     */
    [[nodiscard]] result<size_t> try_send(std::span<const std::byte> buf);

    // ----- options -----

    /**
     * @brief Toggles permission to send datagrams to broadcast addresses. Equivalent
     *        to the POSIX `SO_BROADCAST` socket option.
     *
     * @param on Whether to permit broadcast sends.
     */
    void set_broadcast(bool on) noexcept;

    // ----- multicast (IPv4) -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Joins an IPv4 multicast group so that the socket receives datagrams sent
     *        to that group. Equivalent to the POSIX `IP_ADD_MEMBERSHIP` operation.
     *
     * @param group     Multicast group address to join.
     * @param interface Local interface address (`{}` for default).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure (including `errc::wrong_protocol_type` if
     *         the socket family is not IPv4).
     */
    void join_multicast_v4(ipv4_addr group, ipv4_addr interface = {}) {
        idfxx::unwrap(try_join_multicast_v4(group, interface));
    }
#endif

    /**
     * @brief Joins an IPv4 multicast group so that the socket receives datagrams sent
     *        to that group. Equivalent to the POSIX `IP_ADD_MEMBERSHIP` operation.
     *
     * @param group     Multicast group address to join.
     * @param interface Local interface address (`{}` for default).
     *
     * @return Success, or an error code on failure (including `errc::wrong_protocol_type`
     *         if the socket family is not IPv4).
     */
    [[nodiscard]] result<void> try_join_multicast_v4(ipv4_addr group, ipv4_addr interface = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Leaves an IPv4 multicast group. Equivalent to the POSIX
     *        `IP_DROP_MEMBERSHIP` operation.
     *
     * @param group     Multicast group address to leave.
     * @param interface Local interface address (`{}` for default).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void leave_multicast_v4(ipv4_addr group, ipv4_addr interface = {}) {
        idfxx::unwrap(try_leave_multicast_v4(group, interface));
    }
#endif

    /**
     * @brief Leaves an IPv4 multicast group. Equivalent to the POSIX
     *        `IP_DROP_MEMBERSHIP` operation.
     *
     * @param group     Multicast group address to leave.
     * @param interface Local interface address (`{}` for default).
     *
     * @return Success, or an error code on failure.
     */
    result<void> try_leave_multicast_v4(ipv4_addr group, ipv4_addr interface = {});

    /**
     * @brief Toggles whether outgoing multicast packets are also delivered to local
     *        sockets that have joined the group. Equivalent to the POSIX
     *        `IP_MULTICAST_LOOP` / `IPV6_MULTICAST_LOOP` option (the correct level
     *        is chosen automatically based on the socket family).
     *
     * @param on Whether to enable multicast loopback.
     */
    void set_multicast_loopback(bool on) noexcept;

    /**
     * @brief Sets the hop limit (IPv6) / TTL (IPv4) for outgoing multicast packets.
     *        Equivalent to the POSIX `IPV6_MULTICAST_HOPS` / `IP_MULTICAST_TTL` option
     *        (depending on the socket's family).
     *
     * @param hops Hop limit for multicast packets.
     */
    void set_multicast_hops(uint8_t hops) noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Selects the local interface used for outgoing IPv4 multicast.
     *        Equivalent to the POSIX `IP_MULTICAST_IF` option.
     *
     *        The address is not validated against the host's interface list — an
     *        address that does not correspond to a local interface is stored and
     *        causes subsequent multicast sends to misroute rather than failing here.
     *
     * @param addr Local interface address.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error with `errc::wrong_protocol_type` if the socket family is
     *         not IPv4.
     */
    void set_multicast_interface_v4(ipv4_addr addr) { idfxx::unwrap(try_set_multicast_interface_v4(addr)); }
#endif

    /**
     * @brief Selects the local interface used for outgoing IPv4 multicast.
     *        Equivalent to the POSIX `IP_MULTICAST_IF` option.
     *
     *        The address is not validated against the host's interface list — an
     *        address that does not correspond to a local interface is stored and
     *        causes subsequent multicast sends to misroute rather than failing here.
     *
     * @param addr Local interface address.
     *
     * @retval errc::wrong_protocol_type the socket family is not IPv4
     */
    [[nodiscard]] result<void> try_set_multicast_interface_v4(ipv4_addr addr);

#ifdef CONFIG_LWIP_IPV6
    // ----- multicast (IPv6) -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Joins an IPv6 multicast group. Equivalent to the POSIX
     *        `IPV6_ADD_MEMBERSHIP` operation.
     *
     * @param group    Multicast group address to join.
     * @param if_index Interface index (`0` for default).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` and `CONFIG_LWIP_IPV6`
     *       are enabled.
     * @throws std::system_error on failure (including `errc::wrong_protocol_type` if
     *         the socket family is not IPv6).
     */
    void join_multicast_v6(ipv6_addr group, uint8_t if_index = 0) {
        idfxx::unwrap(try_join_multicast_v6(group, if_index));
    }
#endif

    /**
     * @brief Joins an IPv6 multicast group. Equivalent to the POSIX
     *        `IPV6_ADD_MEMBERSHIP` operation.
     *
     * @param group    Multicast group address to join.
     * @param if_index Interface index (`0` for default).
     *
     * @return Success, or an error code on failure (including `errc::wrong_protocol_type`
     *         if the socket family is not IPv6).
     */
    [[nodiscard]] result<void> try_join_multicast_v6(ipv6_addr group, uint8_t if_index = 0);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Leaves an IPv6 multicast group. Equivalent to the POSIX
     *        `IPV6_DROP_MEMBERSHIP` operation.
     *
     * @param group    Multicast group address to leave.
     * @param if_index Interface index (`0` for default).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` and `CONFIG_LWIP_IPV6`
     *       are enabled.
     * @throws std::system_error on failure.
     */
    void leave_multicast_v6(ipv6_addr group, uint8_t if_index = 0) {
        idfxx::unwrap(try_leave_multicast_v6(group, if_index));
    }
#endif

    /**
     * @brief Leaves an IPv6 multicast group. Equivalent to the POSIX
     *        `IPV6_DROP_MEMBERSHIP` operation.
     *
     * @param group    Multicast group address to leave.
     * @param if_index Interface index (`0` for default).
     *
     * @return Success, or an error code on failure.
     */
    result<void> try_leave_multicast_v6(ipv6_addr group, uint8_t if_index = 0);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Selects the local interface used for outgoing IPv6 multicast.
     *        Equivalent to the POSIX `IPV6_MULTICAST_IF` option.
     *
     *        The index is not validated against the host's interface list — an
     *        index that does not correspond to a local interface is stored and
     *        causes subsequent multicast sends to misroute rather than failing here.
     *
     * @param if_index Interface index.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` and `CONFIG_LWIP_IPV6`
     *       are enabled.
     * @throws std::system_error with `errc::wrong_protocol_type` if the socket family is
     *         not IPv6.
     */
    void set_multicast_interface_v6(uint8_t if_index) { idfxx::unwrap(try_set_multicast_interface_v6(if_index)); }
#endif

    /**
     * @brief Selects the local interface used for outgoing IPv6 multicast.
     *        Equivalent to the POSIX `IPV6_MULTICAST_IF` option.
     *
     *        The index is not validated against the host's interface list — an
     *        index that does not correspond to a local interface is stored and
     *        causes subsequent multicast sends to misroute rather than failing here.
     *
     * @param if_index Interface index.
     *
     * @retval errc::wrong_protocol_type the socket family is not IPv6
     */
    [[nodiscard]] result<void> try_set_multicast_interface_v6(uint8_t if_index);
#endif

private:
    datagram_socket(int fd, address_family fam) noexcept
        : connectionless_socket_base(fd, fam) {}
};

} // namespace idfxx::net

/** @} */ // end of idfxx_net_datagram_socket
