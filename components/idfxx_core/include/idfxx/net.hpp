// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net>
 * @file net.hpp
 * @brief IP address value types for networking.
 *
 * @defgroup idfxx_core_net Networking Types
 * @ingroup idfxx_core
 * @brief IP address and network information value types.
 *
 * Provides IPv4 and IPv6 address types, network information aggregates,
 * and string conversion for use across all networking components.
 * @{
 */

#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net>
 * @brief IPv4 address value type.
 *
 * Stores an IPv4 address in network byte order. Provides construction from
 * individual octets and conversion to string representation.
 *
 * @code
 * auto addr = idfxx::net::ipv4_addr(192, 168, 1, 1);
 * auto s = idfxx::to_string(addr); // "192.168.1.1"
 * @endcode
 */
class ipv4_addr {
public:
    /**
     * @brief Parses an IPv4 address from dotted-decimal notation.
     *
     * Accepts addresses in the form "a.b.c.d" where each octet is a decimal
     * value from 0 to 255 with no leading zeros.
     *
     * @param s The string to parse (e.g. "192.168.1.1").
     * @return The parsed address, or std::nullopt if the string is not valid.
     *
     * @code
     * auto addr = idfxx::net::ipv4_addr::parse("192.168.1.1");
     * if (addr) {
     *     // use *addr
     * }
     * @endcode
     */
    [[nodiscard]] static std::optional<ipv4_addr> parse(std::string_view s) noexcept;

    /**
     * @brief Constructs a zero-initialized IPv4 address (0.0.0.0).
     */
    constexpr ipv4_addr() noexcept = default;

    /**
     * @brief Returns the IPv4 wildcard address (0.0.0.0).
     *
     * The wildcard address is used to bind a socket to all local IPv4
     * interfaces. Equivalent to the POSIX `INADDR_ANY` constant.
     *
     * @return The wildcard IPv4 address.
     */
    [[nodiscard]] static constexpr ipv4_addr any() noexcept { return ipv4_addr{}; }

    /**
     * @brief Constructs an IPv4 address from a raw network-byte-order value.
     *
     * @param addr The IPv4 address in network byte order.
     */
    constexpr explicit ipv4_addr(uint32_t addr) noexcept
        : _addr(addr) {}

    /**
     * @brief Constructs an IPv4 address from individual octets.
     *
     * @param a First octet (most significant).
     * @param b Second octet.
     * @param c Third octet.
     * @param d Fourth octet (least significant).
     */
    constexpr ipv4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept
        : _addr(
              static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16) |
              (static_cast<uint32_t>(d) << 24)
          ) {}

    /**
     * @brief Returns the raw address value in network byte order.
     *
     * @return The IPv4 address as a 32-bit integer in network byte order.
     */
    [[nodiscard]] constexpr uint32_t addr() const noexcept { return _addr; }

    /**
     * @brief Tests whether this is a zero (unset) address.
     *
     * @return True if the address is 0.0.0.0.
     */
    [[nodiscard]] constexpr bool is_any() const noexcept { return _addr == 0; }

    /**
     * @brief Compares two IPv4 addresses for equality.
     */
    [[nodiscard]] constexpr bool operator==(const ipv4_addr&) const noexcept = default;

private:
    uint32_t _addr = 0;
};

/**
 * @headerfile <idfxx/net>
 * @brief IPv6 address value type.
 *
 * Stores an IPv6 address as four 32-bit words plus an optional zone ID.
 *
 * @code
 * auto addr = idfxx::net::ipv6_addr({0xfe800000, 0, 0, 1});
 * auto s = idfxx::to_string(addr); // "fe80::1"
 * @endcode
 */
class ipv6_addr {
public:
    /**
     * @brief Parses an IPv6 address from its text representation.
     *
     * Accepts standard colon-separated hex format with optional `::` compression
     * and an optional `%zone` suffix (e.g. "fe80::1%3"). Each group is 1–4
     * hex digits. Exactly one `::` may appear, representing one or more
     * consecutive all-zero groups.
     *
     * @param s The string to parse (e.g. "2001:db8::1", "::1", "fe80::1%3").
     * @return The parsed address, or std::nullopt if the string is not valid.
     *
     * @code
     * auto addr = idfxx::net::ipv6_addr::parse("fe80::1%3");
     * if (addr) {
     *     // use *addr
     * }
     * @endcode
     */
    [[nodiscard]] static std::optional<ipv6_addr> parse(std::string_view s) noexcept;

    /**
     * @brief Constructs a zero-initialized IPv6 address (::).
     */
    constexpr ipv6_addr() noexcept = default;

    /**
     * @brief Returns the IPv6 wildcard address (::).
     *
     * The wildcard address is used to bind a socket to all local IPv6
     * interfaces. Equivalent to the POSIX `IN6ADDR_ANY_INIT` constant.
     *
     * @return The wildcard IPv6 address.
     */
    [[nodiscard]] static constexpr ipv6_addr any() noexcept { return ipv6_addr{}; }

    /**
     * @brief Constructs an IPv6 address from four 32-bit words.
     *
     * @param words The four 32-bit words of the IPv6 address.
     * @param zone Optional zone ID (default 0).
     */
    constexpr explicit ipv6_addr(std::array<uint32_t, 4> words, uint8_t zone = 0) noexcept
        : _addr(words)
        , _zone(zone) {}

    /**
     * @brief Returns the four 32-bit words of the address.
     *
     * @return Reference to the address words array.
     */
    [[nodiscard]] constexpr const std::array<uint32_t, 4>& addr() const noexcept { return _addr; }

    /**
     * @brief Returns the zone ID.
     *
     * @return The zone ID.
     */
    [[nodiscard]] constexpr uint8_t zone() const noexcept { return _zone; }

    /**
     * @brief Tests whether this is a zero (unset) address.
     *
     * @return True if all address words are zero.
     */
    [[nodiscard]] constexpr bool is_any() const noexcept {
        return _addr[0] == 0 && _addr[1] == 0 && _addr[2] == 0 && _addr[3] == 0;
    }

    /**
     * @brief Compares two IPv6 addresses for equality (including zone).
     */
    [[nodiscard]] constexpr bool operator==(const ipv6_addr&) const noexcept = default;

private:
    std::array<uint32_t, 4> _addr = {};
    uint8_t _zone = 0;
};

/**
 * @headerfile <idfxx/net>
 * @brief Concept matching either ipv4_addr or ipv6_addr.
 *
 * Used to constrain templates that accept any IP address type.
 */
template<typename T>
concept ip_address = std::same_as<T, ipv4_addr> || std::same_as<T, ipv6_addr>;

/**
 * @headerfile <idfxx/net>
 * @brief IPv4 network interface information.
 *
 * Contains the IPv4 address, netmask, and gateway for an interface.
 */
struct ipv4_info {
    ipv4_addr ip;      /*!< Interface IPv4 address. */
    ipv4_addr netmask; /*!< Interface IPv4 netmask. */
    ipv4_addr gateway; /*!< Interface IPv4 gateway address. */

    /**
     * @brief Compares two ipv4_info structs for equality.
     */
    [[nodiscard]] constexpr bool operator==(const ipv4_info&) const noexcept = default;
};

/**
 * @headerfile <idfxx/net>
 * @brief IPv6 network interface information.
 *
 * Contains the IPv6 address for an interface.
 */
struct ipv6_info {
    ipv6_addr ip; /*!< Interface IPv6 address. */
};

/// @cond INTERNAL
// ----- backward-compatibility aliases -----
//
// `ipv4_addr` / `ipv6_addr` / `ipv4_info` / `ipv6_info` were originally named
// `ip4_addr` / `ip6_addr` / `ip4_info` / `ip6_info`. The aliases preserve the
// old spelling for code written against earlier releases.
using ip4_addr [[deprecated("use ipv4_addr")]] = ipv4_addr;
using ip6_addr [[deprecated("use ipv6_addr")]] = ipv6_addr;
using ip4_info [[deprecated("use ipv4_info")]] = ipv4_info;
using ip6_info [[deprecated("use ipv6_info")]] = ipv6_info;
/// @endcond

} // namespace idfxx::net

// =============================================================================
// String conversions (in idfxx namespace)
// =============================================================================

namespace idfxx {

/**
 * @headerfile <idfxx/net>
 * @brief Returns the dotted-decimal string representation of an IPv4 address.
 *
 * @param addr The IPv4 address to convert.
 * @return A string like "192.168.1.1".
 */
[[nodiscard]] std::string to_string(net::ipv4_addr addr);

/**
 * @headerfile <idfxx/net>
 * @brief Returns the string representation of an IPv6 address.
 *
 * Uses RFC 5952 compressed format with `::` for the longest run of zero groups.
 *
 * @param addr The IPv6 address to convert.
 * @return A string like "fe80::1".
 */
[[nodiscard]] std::string to_string(net::ipv6_addr addr);

/**
 * @headerfile <idfxx/net>
 * @brief Returns the string representation of IPv4 network information.
 *
 * @param info The IP info to convert.
 * @return A string like "ip=192.168.1.1, netmask=255.255.255.0, gw=192.168.1.1".
 */
[[nodiscard]] std::string to_string(net::ipv4_info info);

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/// @cond INTERNAL
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::net::ipv4_addr> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::net::ipv4_addr addr, FormatContext& ctx) const {
        auto s = idfxx::to_string(addr);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::net::ipv6_addr> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::net::ipv6_addr addr, FormatContext& ctx) const {
        auto s = idfxx::to_string(addr);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::net::ipv4_info> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::net::ipv4_info info, FormatContext& ctx) const {
        auto s = idfxx::to_string(info);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/// @endcond
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_core_net
