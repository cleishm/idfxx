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
 * auto addr = idfxx::net::ip4_addr(192, 168, 1, 1);
 * auto s = idfxx::to_string(addr); // "192.168.1.1"
 * @endcode
 */
class ip4_addr {
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
     * auto addr = idfxx::net::ip4_addr::parse("192.168.1.1");
     * if (addr) {
     *     // use *addr
     * }
     * @endcode
     */
    [[nodiscard]] static std::optional<ip4_addr> parse(std::string_view s) noexcept;

    /**
     * @brief Constructs a zero-initialized IPv4 address (0.0.0.0).
     */
    constexpr ip4_addr() noexcept = default;

    /**
     * @brief Constructs an IPv4 address from a raw network-byte-order value.
     *
     * @param addr The IPv4 address in network byte order.
     */
    constexpr explicit ip4_addr(uint32_t addr) noexcept
        : _addr(addr) {}

    /**
     * @brief Constructs an IPv4 address from individual octets.
     *
     * @param a First octet (most significant).
     * @param b Second octet.
     * @param c Third octet.
     * @param d Fourth octet (least significant).
     */
    constexpr ip4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d) noexcept
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
    [[nodiscard]] constexpr bool operator==(const ip4_addr&) const noexcept = default;

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
 * auto addr = idfxx::net::ip6_addr({0xfe800000, 0, 0, 1});
 * auto s = idfxx::to_string(addr); // "fe80::1"
 * @endcode
 */
class ip6_addr {
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
     * auto addr = idfxx::net::ip6_addr::parse("fe80::1%3");
     * if (addr) {
     *     // use *addr
     * }
     * @endcode
     */
    [[nodiscard]] static std::optional<ip6_addr> parse(std::string_view s) noexcept;

    /**
     * @brief Constructs a zero-initialized IPv6 address (::).
     */
    constexpr ip6_addr() noexcept = default;

    /**
     * @brief Constructs an IPv6 address from four 32-bit words.
     *
     * @param words The four 32-bit words of the IPv6 address.
     * @param zone Optional zone ID (default 0).
     */
    constexpr explicit ip6_addr(std::array<uint32_t, 4> words, uint8_t zone = 0) noexcept
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
    [[nodiscard]] constexpr bool operator==(const ip6_addr&) const noexcept = default;

private:
    std::array<uint32_t, 4> _addr = {};
    uint8_t _zone = 0;
};

/**
 * @headerfile <idfxx/net>
 * @brief IPv4 network interface information.
 *
 * Contains the IPv4 address, netmask, and gateway for an interface.
 */
struct ip4_info {
    ip4_addr ip;      /*!< Interface IPv4 address. */
    ip4_addr netmask; /*!< Interface IPv4 netmask. */
    ip4_addr gateway; /*!< Interface IPv4 gateway address. */

    /**
     * @brief Compares two ip4_info structs for equality.
     */
    [[nodiscard]] constexpr bool operator==(const ip4_info&) const noexcept = default;
};

/**
 * @headerfile <idfxx/net>
 * @brief IPv6 network interface information.
 *
 * Contains the IPv6 address for an interface.
 */
struct ip6_info {
    ip6_addr ip; /*!< Interface IPv6 address. */
};

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
[[nodiscard]] std::string to_string(net::ip4_addr addr);

/**
 * @headerfile <idfxx/net>
 * @brief Returns the string representation of an IPv6 address.
 *
 * Uses RFC 5952 compressed format with `::` for the longest run of zero groups.
 *
 * @param addr The IPv6 address to convert.
 * @return A string like "fe80::1".
 */
[[nodiscard]] std::string to_string(net::ip6_addr addr);

/**
 * @headerfile <idfxx/net>
 * @brief Returns the string representation of IPv4 network information.
 *
 * @param info The IP info to convert.
 * @return A string like "ip=192.168.1.1, netmask=255.255.255.0, gw=192.168.1.1".
 */
[[nodiscard]] std::string to_string(net::ip4_info info);

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/// @cond INTERNAL
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::net::ip4_addr> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::net::ip4_addr addr, FormatContext& ctx) const {
        auto s = idfxx::to_string(addr);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::net::ip6_addr> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::net::ip6_addr addr, FormatContext& ctx) const {
        auto s = idfxx::to_string(addr);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::net::ip4_info> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::net::ip4_info info, FormatContext& ctx) const {
        auto s = idfxx::to_string(info);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/// @endcond
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_core_net
