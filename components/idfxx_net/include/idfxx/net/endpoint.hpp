// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/endpoint>
 * @file endpoint.hpp
 * @brief IPv4/IPv6 transport endpoint value type.
 *
 * @defgroup idfxx_net_endpoint Endpoint
 * @ingroup idfxx_net
 * @brief Address/port pair used by sockets, listeners, and the resolver.
 * @{
 */

#include <idfxx/net>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/endpoint>
 * @brief Address family.
 *
 * Where a "no preference" or "any family" semantic is needed (e.g. resolver
 * hints, a default-constructed endpoint), use `std::optional<family>` rather
 * than a sentinel value.
 */
enum class family : int {
    ipv4 = 2,  ///< IPv4.
    ipv6 = 10, ///< IPv6.
};

/** @brief Port number type for transport endpoints. */
using port_number = uint16_t;

/**
 * @headerfile <idfxx/net/endpoint>
 * @brief One direction of a bidirectional transport.
 *
 * Used by half-shutdown operations to identify which side of the connection
 * to close. To act on both directions, call the no-argument overload of the
 * relevant method.
 */
enum class direction : int {
    receive, ///< The receive direction.
    send,    ///< The send direction.
};

/**
 * @headerfile <idfxx/net/endpoint>
 * @brief Differentiated Services Code Point — the top 6 bits of the IPv4/IPv6
 *        traffic-class byte, per RFC 2474 / RFC 4594.
 *
 * Named values cover the standard per-hop behaviours (default, class selectors,
 * assured-forwarding classes, expedited forwarding, voice-admit). Custom 6-bit
 * values can be passed as `static_cast<dscp>(n)`; only the low 6 bits are used.
 */
enum class dscp : uint8_t {
    default_ = 0, ///< CS0 — best-effort (default).

    // Class selectors (RFC 2474 §4.2.2 — backwards-compat with legacy IP precedence).
    cs1 = 8,
    cs2 = 16,
    cs3 = 24,
    cs4 = 32,
    cs5 = 40,
    cs6 = 48,
    cs7 = 56,

    // Assured forwarding (RFC 2597).
    af11 = 10,
    af12 = 12,
    af13 = 14,
    af21 = 18,
    af22 = 20,
    af23 = 22,
    af31 = 26,
    af32 = 28,
    af33 = 30,
    af41 = 34,
    af42 = 36,
    af43 = 38,

    voice_admit = 44, ///< Voice-admit (RFC 5865).
    ef = 46,          ///< Expedited forwarding (RFC 3246).
};

/**
 * @headerfile <idfxx/net/endpoint>
 * @brief Address/port pair identifying a transport endpoint.
 *
 * An `endpoint` carries either an IPv4 or IPv6 address and a port number,
 * or none — a default-constructed endpoint holds no address.
 *
 * @code
 * idfxx::net::endpoint server(idfxx::net::ipv4_addr(192, 168, 1, 10), 8080);
 * if (auto e = idfxx::net::endpoint::parse("[fe80::1]:80")) {
 *     // *e is a v6 endpoint
 * }
 * @endcode
 */
class endpoint {
public:
    /** @brief Constructs an endpoint that holds no address. */
    constexpr endpoint() noexcept = default;

    /**
     * @brief Constructs an IPv4 endpoint.
     *
     * @param addr IPv4 address.
     * @param port Port number in host byte order.
     */
    constexpr endpoint(ipv4_addr addr, port_number port) noexcept
        : _ep(std::pair<ipv4_addr, port_number>{addr, port}) {}

    /**
     * @brief Constructs an IPv6 endpoint.
     *
     * @param addr IPv6 address.
     * @param port Port number in host byte order.
     */
    constexpr endpoint(ipv6_addr addr, port_number port) noexcept
        : _ep(std::pair<ipv6_addr, port_number>{addr, port}) {}

    /**
     * @brief Parses an endpoint from a `host:port` or `[host]:port` string.
     *
     * Accepts:
     * - `1.2.3.4:80` for IPv4
     * - `[fe80::1]:80` (with bracketed v6 address) for IPv6
     *
     * The host part is parsed by `ipv4_addr::parse` or `ipv6_addr::parse`;
     * names are not resolved.
     *
     * @param s The string to parse.
     * @return The parsed endpoint, or `std::nullopt` on failure.
     */
    [[nodiscard]] static std::optional<endpoint> parse(std::string_view s) noexcept;

    /**
     * @brief Returns the address family of this endpoint.
     *
     * @return `family::ipv4` or `family::ipv6`, or `std::nullopt` for a
     *         default-constructed endpoint.
     */
    [[nodiscard]] constexpr std::optional<enum family> family() const noexcept {
        if (std::holds_alternative<std::pair<ipv4_addr, port_number>>(_ep)) {
            return family::ipv4;
        }
        if (std::holds_alternative<std::pair<ipv6_addr, port_number>>(_ep)) {
            return family::ipv6;
        }
        return std::nullopt;
    }

    /**
     * @brief Returns the port number in host byte order.
     *
     * @return The port number, or 0 if the endpoint is default-constructed.
     */
    [[nodiscard]] constexpr port_number port() const noexcept {
        if (auto* p = std::get_if<std::pair<ipv4_addr, port_number>>(&_ep)) {
            return p->second;
        }
        if (auto* p = std::get_if<std::pair<ipv6_addr, port_number>>(&_ep)) {
            return p->second;
        }
        return 0;
    }

    /**
     * @brief Returns the address held by this endpoint, if it is of the
     *        requested type.
     *
     * @tparam T `ipv4_addr` or `ipv6_addr`.
     * @return The address, or `std::nullopt` if the endpoint is
     *         default-constructed or holds an address of a different family.
     *
     * @code
     * if (auto v4 = ep.address<idfxx::net::ipv4_addr>()) {
     *     // *v4 is the IPv4 address
     * }
     * @endcode
     */
    template<ip_address T>
    [[nodiscard]] constexpr std::optional<T> address() const noexcept {
        if (auto* p = std::get_if<std::pair<T, port_number>>(&_ep)) {
            return p->first;
        }
        return std::nullopt;
    }

    /** @brief Compares two endpoints for equality. */
    [[nodiscard]] constexpr bool operator==(const endpoint&) const noexcept = default;

private:
    std::variant<std::monostate, std::pair<ipv4_addr, port_number>, std::pair<ipv6_addr, port_number>> _ep;
};

/**
 * @headerfile <idfxx/net/endpoint>
 * @brief A datagram received together with the sender's endpoint.
 *
 * Returned by `try_recv_from` on connectionless sockets and netconn
 * connections. `data` is a sub-span of the buffer the caller passed in, sized
 * to the bytes actually received.
 */
struct datagram {
    std::span<std::byte> data; ///< Filled portion of the caller's receive buffer.
    endpoint from;             ///< Source endpoint of the datagram.
};

} // namespace idfxx::net

namespace idfxx {

/**
 * @headerfile <idfxx/net/endpoint>
 * @brief Returns the canonical string representation of an endpoint.
 *
 * Formats as `host:port` for IPv4 and `[host]:port` for IPv6. A
 * default-constructed endpoint formats as `<empty>`.
 *
 * @param ep The endpoint to format.
 * @return The string representation.
 */
[[nodiscard]] std::string to_string(const net::endpoint& ep);

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/// @cond INTERNAL
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::net::endpoint> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const idfxx::net::endpoint& ep, FormatContext& ctx) const {
        auto s = idfxx::to_string(ep);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/// @endcond
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_net_endpoint
