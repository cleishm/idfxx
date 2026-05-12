// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"
#include "detail/sockopt_apply.hpp"

#include <idfxx/net/raw_socket>

#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <utility>

namespace idfxx::net {

namespace {

result<void> apply_config(raw_socket& s, const raw_socket::config& cfg) {
    detail::common_config common{
        .family = cfg.family,
        .non_blocking = cfg.non_blocking,
        .recv_timeout = cfg.recv_timeout,
        .send_timeout = cfg.send_timeout,
        .bind_to_device = cfg.bind_to_device,
#ifdef CONFIG_LWIP_IPV6
        .ipv6_only = cfg.ipv6_only,
#endif
    };
    return detail::apply_common_config(s.idf_handle(), common);
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
raw_socket::raw_socket(ip_protocol protocol)
    : raw_socket(idfxx::unwrap(make(protocol))) {}

raw_socket::raw_socket(ip_protocol protocol, address_family fam)
    : raw_socket(idfxx::unwrap(make(protocol, fam))) {}

raw_socket::raw_socket(const config& cfg)
    : raw_socket(idfxx::unwrap(make(cfg))) {}
#endif

result<raw_socket> raw_socket::make(ip_protocol protocol) {
    return make(config{.protocol = protocol});
}

result<raw_socket> raw_socket::make(ip_protocol protocol, address_family fam) {
    return make(config{.protocol = protocol, .family = fam});
}

result<raw_socket> raw_socket::make(const config& cfg) {
    int af = detail::family_to_af(cfg.family);
    if (af == AF_UNSPEC) {
        return error(errc::invalid_argument);
    }
    int fd = lwip_socket(af, SOCK_RAW, static_cast<int>(cfg.protocol));
    if (fd < 0) {
        return error(errno_to_error_code(errno));
    }
    raw_socket s(raw_socket::from_fd_t{}, fd, cfg.family);
    if (auto r = apply_config(s, cfg); !r) {
        return error(r.error());
    }
    return s;
}

result<size_t> raw_socket::try_send(std::span<const std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    ssize_t n = lwip_send(_fd, buf.data(), buf.size(), 0);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    return static_cast<size_t>(n);
}

} // namespace idfxx::net
