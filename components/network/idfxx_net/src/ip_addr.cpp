// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <array>
#include <cstring>
#include <lwip/ip_addr.h>
#include <lwip/opt.h>

namespace idfxx::net::detail {

std::optional<std::pair<::ip_addr_t, uint16_t>> endpoint_to_ip_addr(const endpoint& ep) noexcept {
    ::ip_addr_t out{};
    if (auto v4 = ep.address<ipv4_addr>()) {
        ipv4_to_ip_addr(*v4, out);
        return std::pair{out, ep.port()};
    }
#ifdef CONFIG_LWIP_IPV6
    if (auto v6 = ep.address<ipv6_addr>()) {
        ipv6_to_ip_addr(*v6, out);
        return std::pair{out, ep.port()};
    }
#endif
    return std::nullopt;
}

std::optional<endpoint> ip_addr_to_endpoint(const ::ip_addr_t* addr, uint16_t port) noexcept {
    if (addr == nullptr) {
        return std::nullopt;
    }
    if (IP_IS_V4(addr)) {
        ipv4_addr v4(ip_addr_get_ip4_u32(addr));
        return endpoint(v4, port);
    }
#ifdef CONFIG_LWIP_IPV6
    if (IP_IS_V6(addr)) {
        std::array<uint32_t, 4> words;
        std::memcpy(words.data(), ip_2_ip6(addr)->addr, 16);
        ipv6_addr v6(words, static_cast<uint8_t>(ip_2_ip6(addr)->zone));
        return endpoint(v6, port);
    }
#endif
    return std::nullopt;
}

} // namespace idfxx::net::detail
