// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"
#include "detail/sockopt_apply.hpp"

#include <idfxx/net/datagram_socket>

#include <cstring>
#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <tuple>
#include <utility>

namespace idfxx::net {

namespace {

result<void> apply_config(datagram_socket& s, const datagram_socket::config& cfg) {
    int fd = s.idf_handle();
    detail::common_config common{
        .family = cfg.family,
        .non_blocking = cfg.non_blocking,
        .recv_timeout = cfg.recv_timeout,
        .send_timeout = cfg.send_timeout,
        .reuse_address = cfg.reuse_address,
        .bind_to_device = cfg.bind_to_device,
#ifdef CONFIG_LWIP_IPV6
        .ipv6_only = cfg.ipv6_only,
#endif
    };
    if (auto r = detail::apply_common_config(fd, common); !r) {
        return r;
    }
    if (cfg.broadcast) {
        if (auto r = detail::set_int_option(fd, SOL_SOCKET, SO_BROADCAST, 1); !r) {
            return r;
        }
    }
    return {};
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
datagram_socket::datagram_socket()
    : datagram_socket(idfxx::unwrap(make())) {}

datagram_socket::datagram_socket(address_family fam)
    : datagram_socket(idfxx::unwrap(make(fam))) {}

datagram_socket::datagram_socket(const config& cfg)
    : datagram_socket(idfxx::unwrap(make(cfg))) {}
#endif

result<datagram_socket> datagram_socket::make() {
    return make(config{});
}

result<datagram_socket> datagram_socket::make(address_family fam) {
    return make(config{.family = fam});
}

result<datagram_socket> datagram_socket::make(const config& cfg) {
    int af = detail::family_to_af(cfg.family);
    if (af == AF_UNSPEC) {
        return error(errc::invalid_argument);
    }
    int fd = lwip_socket(af, SOCK_DGRAM, 0);
    if (fd < 0) {
        return error(errno_to_error_code(errno));
    }
    datagram_socket s(fd, cfg.family);
    if (auto r = apply_config(s, cfg); !r) {
        return error(r.error());
    }
    return s;
}

result<size_t> datagram_socket::try_send(std::span<const std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    ssize_t n = lwip_send(_fd, buf.data(), buf.size(), 0);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    return static_cast<size_t>(n);
}

void datagram_socket::set_broadcast(bool on) noexcept {
    if (_fd < 0) {
        return;
    }
    std::ignore = detail::set_int_option(_fd, SOL_SOCKET, SO_BROADCAST, on ? 1 : 0);
}

namespace {

result<void> ipv4_membership(int fd, address_family fam, int op, ipv4_addr group, ipv4_addr interface) {
    if (fd < 0) {
        return error(errc::invalid_state);
    }
    if (fam != address_family::ipv4) {
        return error(errc::wrong_protocol_type);
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = group.addr();
    mreq.imr_interface.s_addr = interface.addr();
    if (lwip_setsockopt(fd, IPPROTO_IP, op, &mreq, sizeof(mreq)) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

#ifdef CONFIG_LWIP_IPV6
result<void> ipv6_membership(int fd, address_family fam, int op, ipv6_addr group, uint8_t if_index) {
    if (fd < 0) {
        return error(errc::invalid_state);
    }
    if (fam != address_family::ipv6) {
        return error(errc::wrong_protocol_type);
    }
    ipv6_mreq mreq{};
    std::memcpy(mreq.ipv6mr_multiaddr.s6_addr, group.addr().data(), 16);
    mreq.ipv6mr_interface = if_index;
    if (lwip_setsockopt(fd, IPPROTO_IPV6, op, &mreq, sizeof(mreq)) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}
#endif

} // namespace

result<void> datagram_socket::try_join_multicast_v4(ipv4_addr group, ipv4_addr interface) {
    return ipv4_membership(_fd, _family, IP_ADD_MEMBERSHIP, group, interface);
}

result<void> datagram_socket::try_leave_multicast_v4(ipv4_addr group, ipv4_addr interface) {
    return ipv4_membership(_fd, _family, IP_DROP_MEMBERSHIP, group, interface);
}

void datagram_socket::set_multicast_loopback(bool on) noexcept {
    if (_fd < 0) {
        return;
    }
#ifdef CONFIG_LWIP_IPV6
    if (_family == address_family::ipv6) {
        std::ignore = detail::set_int_option(_fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, on ? 1 : 0);
        return;
    }
#endif
    std::ignore = detail::set_int_option(_fd, IPPROTO_IP, IP_MULTICAST_LOOP, on ? 1 : 0);
}

void datagram_socket::set_multicast_hops(uint8_t hops) noexcept {
    if (_fd < 0) {
        return;
    }
#ifdef CONFIG_LWIP_IPV6
    if (_family == address_family::ipv6) {
        std::ignore = detail::set_int_option(_fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, hops);
        return;
    }
#endif
    std::ignore = detail::set_int_option(_fd, IPPROTO_IP, IP_MULTICAST_TTL, hops);
}

result<void> datagram_socket::try_set_multicast_interface_v4(ipv4_addr addr) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    if (_family != address_family::ipv4) {
        return error(errc::wrong_protocol_type);
    }
    in_addr a;
    a.s_addr = addr.addr();
    if (lwip_setsockopt(_fd, IPPROTO_IP, IP_MULTICAST_IF, &a, sizeof(a)) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

#ifdef CONFIG_LWIP_IPV6
result<void> datagram_socket::try_join_multicast_v6(ipv6_addr group, uint8_t if_index) {
    return ipv6_membership(_fd, _family, IPV6_ADD_MEMBERSHIP, group, if_index);
}

result<void> datagram_socket::try_leave_multicast_v6(ipv6_addr group, uint8_t if_index) {
    return ipv6_membership(_fd, _family, IPV6_DROP_MEMBERSHIP, group, if_index);
}

result<void> datagram_socket::try_set_multicast_interface_v6(uint8_t if_index) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    if (_family != address_family::ipv6) {
        return error(errc::wrong_protocol_type);
    }
    return detail::set_int_option(_fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, if_index);
}
#endif

} // namespace idfxx::net
