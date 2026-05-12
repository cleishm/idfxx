// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockaddr.hpp"

#include <cstddef>
#include <cstring>
#include <errno.h>
#include <lwip/inet.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <optional>
#include <utility>

// Verify family values match lwIP/POSIX AF_* constants.
static_assert(std::to_underlying(idfxx::net::family::ipv4) == AF_INET);
#ifdef CONFIG_LWIP_IPV6
static_assert(std::to_underlying(idfxx::net::family::ipv6) == AF_INET6);
#endif

namespace idfxx::net::detail {

int family_to_af(std::optional<family> f) noexcept {
    if (!f) {
        return AF_UNSPEC;
    }
    switch (*f) {
    case family::ipv4:
        return AF_INET;
    case family::ipv6:
#ifdef CONFIG_LWIP_IPV6
        return AF_INET6;
#else
        return AF_UNSPEC;
#endif
    }
    return AF_UNSPEC;
}

std::optional<endpoint> from_sockaddr(const sockaddr* sa, socklen_t len) noexcept {
    if (sa == nullptr) {
        return std::nullopt;
    }
    if (sa->sa_family == AF_INET) {
        if (len < socklen_t(sizeof(sockaddr_in))) {
            return std::nullopt;
        }
        const auto& sin = *reinterpret_cast<const sockaddr_in*>(sa);
        // sin_addr.s_addr is in network byte order — same convention as ipv4_addr::addr().
        ipv4_addr addr(sin.sin_addr.s_addr);
        port_number port = lwip_ntohs(sin.sin_port);
        return endpoint(addr, port);
    }
#ifdef CONFIG_LWIP_IPV6
    if (sa->sa_family == AF_INET6) {
        if (len < socklen_t(sizeof(sockaddr_in6))) {
            return std::nullopt;
        }
        const auto& sin6 = *reinterpret_cast<const sockaddr_in6*>(sa);
        std::array<uint32_t, 4> words;
        std::memcpy(words.data(), sin6.sin6_addr.s6_addr, 16);
        // sin6_scope_id is a 32-bit interface index in lwIP; ipv6_addr stores
        // an 8-bit zone, so truncate.
        ipv6_addr addr(words, static_cast<uint8_t>(sin6.sin6_scope_id));
        port_number port = lwip_ntohs(sin6.sin6_port);
        return endpoint(addr, port);
    }
#endif
    return std::nullopt;
}

socklen_t to_sockaddr(const endpoint& ep, sockaddr_storage& out) noexcept {
    if (auto v4 = ep.address<ipv4_addr>()) {
        auto& sin = reinterpret_cast<sockaddr_in&>(out);
        std::memset(&sin, 0, sizeof(sin));
        sin.sin_len = sizeof(sockaddr_in);
        sin.sin_family = AF_INET;
        sin.sin_port = lwip_htons(ep.port());
        sin.sin_addr.s_addr = v4->addr();
        return sizeof(sockaddr_in);
    }
#ifdef CONFIG_LWIP_IPV6
    if (auto v6 = ep.address<ipv6_addr>()) {
        auto& sin6 = reinterpret_cast<sockaddr_in6&>(out);
        std::memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_len = sizeof(sockaddr_in6);
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port = lwip_htons(ep.port());
        std::memcpy(sin6.sin6_addr.s6_addr, v6->addr().data(), 16);
        sin6.sin6_scope_id = v6->zone();
        return sizeof(sockaddr_in6);
    }
#endif
    return 0;
}

result<endpoint> get_local_endpoint(int fd) noexcept {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    if (lwip_getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) {
        return error(errno_to_error_code(errno));
    }
    auto ep = from_sockaddr(reinterpret_cast<const sockaddr*>(&ss), len);
    if (!ep) {
        return error(errc::not_supported);
    }
    return *ep;
}

result<endpoint> get_peer_endpoint(int fd) noexcept {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    if (lwip_getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) {
        return error(errno_to_error_code(errno));
    }
    auto ep = from_sockaddr(reinterpret_cast<const sockaddr*>(&ss), len);
    if (!ep) {
        return error(errc::not_supported);
    }
    return *ep;
}

} // namespace idfxx::net::detail
