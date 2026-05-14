// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal-only header. Provides conversions between the public idfxx::net
// endpoint type and lwIP's `ip_addr_t`. lwIP types never appear in the public
// API.

#include <idfxx/net/endpoint>

#include <cstring>
#include <lwip/ip_addr.h>
#include <lwip/opt.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace idfxx::net::detail {

/// Populate `out` with the v4 address.
inline void ipv4_to_ip_addr(ipv4_addr addr, ::ip_addr_t& out) noexcept {
    IP_SET_TYPE_VAL(out, IPADDR_TYPE_V4);
    ip_2_ip4(&out)->addr = addr.addr();
}

#ifdef CONFIG_LWIP_IPV6
/// Populate `out` with the v6 address (including zone).
inline void ipv6_to_ip_addr(ipv6_addr addr, ::ip_addr_t& out) noexcept {
    IP_SET_TYPE_VAL(out, IPADDR_TYPE_V6);
    std::memcpy(ip_2_ip6(&out)->addr, addr.addr().data(), 16);
    ip_2_ip6(&out)->zone = addr.zone();
}
#endif

/// Convert an endpoint to an lwIP ip_addr_t + port. Returns std::nullopt if
/// the endpoint holds no address.
[[nodiscard]] std::optional<std::pair<::ip_addr_t, uint16_t>> endpoint_to_ip_addr(const endpoint& ep) noexcept;

/// Convert an lwIP ip_addr_t + port to an endpoint. Returns std::nullopt if
/// `addr` is null or its family is not one we recognise (v4 or v6).
[[nodiscard]] std::optional<endpoint> ip_addr_to_endpoint(const ::ip_addr_t* addr, uint16_t port) noexcept;

} // namespace idfxx::net::detail
