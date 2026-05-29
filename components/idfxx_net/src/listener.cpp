// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/select.hpp"
#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"
#include "detail/sockopt_apply.hpp"

#include <idfxx/net/listener>

#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <utility>

namespace idfxx::net {

namespace {

result<void> apply_listener_config(listener& a, const listener::config& cfg) {
    detail::common_config common{
        .family = cfg.family,
        .non_blocking = cfg.non_blocking,
        .reuse_address = cfg.reuse_address,
        .bind_to_device = cfg.bind_to_device,
#ifdef CONFIG_LWIP_IPV6
        .ipv6_only = cfg.ipv6_only,
#endif
    };
    return detail::apply_common_config(a.idf_handle(), common);
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
listener::listener(const endpoint& bind_addr)
    : listener(idfxx::unwrap(make(bind_addr))) {}

listener::listener(const endpoint& bind_addr, const config& cfg)
    : listener(idfxx::unwrap(make(bind_addr, cfg))) {}

listener::listener(port_number port)
    : listener(idfxx::unwrap(make(port))) {}

listener::listener(port_number port, const config& cfg)
    : listener(idfxx::unwrap(make(port, cfg))) {}
#endif

result<listener> listener::make(const endpoint& bind_addr) {
    return make(bind_addr, config{});
}

result<listener> listener::make(const endpoint& bind_addr, const config& cfg) {
    auto fam = bind_addr.family();

    int af = detail::family_to_af(fam);
    int fd = lwip_socket(af, SOCK_STREAM, 0);
    if (fd < 0) {
        return error(errno_to_error_code(errno));
    }
    listener a(fd, fam);

    config c = cfg;
    c.family = fam;
    if (auto r = apply_listener_config(a, c); !r) {
        return error(r.error());
    }

    sockaddr_storage ss{};
    auto len = detail::to_sockaddr(bind_addr, ss);
    if (len == 0) {
        return error(errc::invalid_argument);
    }
    if (lwip_bind(fd, reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        return error(errno_to_error_code(errno));
    }
    if (lwip_listen(fd, c.backlog) != 0) {
        return error(errno_to_error_code(errno));
    }
    return a;
}

result<listener> listener::make(port_number port) {
#ifdef CONFIG_LWIP_IPV6
    return make(port, {.family = address_family::ipv6});
#else
    return make(port, {.family = address_family::ipv4});
#endif
}

result<listener> listener::make(port_number port, const config& cfg) {
    if (cfg.family == address_family::ipv6) {
        return make(endpoint{ipv6_addr::any(), port}, cfg);
    }
    return make(endpoint{ipv4_addr::any(), port}, cfg);
}

result<stream_socket> listener::try_accept() {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    int client_fd = lwip_accept(_fd, nullptr, nullptr);
    if (client_fd < 0) {
        return error(errno_to_error_code(errno));
    }
    return stream_socket(client_fd, _family);
}

result<listener::accepted_connection> listener::try_accept_with_peer() {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    int client_fd = lwip_accept(_fd, reinterpret_cast<sockaddr*>(&ss), &len);
    if (client_fd < 0) {
        return error(errno_to_error_code(errno));
    }
    // Take ownership of the accepted descriptor immediately so it is released
    // if the (near-impossible) peer-address parse below fails.
    stream_socket sock(client_fd, _family);
    auto peer = detail::from_sockaddr(reinterpret_cast<const sockaddr*>(&ss), len);
    if (!peer) {
        return error(errc::invalid_argument);
    }
    return accepted_connection{std::move(sock), *peer};
}

result<stream_socket> listener::_try_accept_for(std::chrono::milliseconds timeout) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    auto wait_r = detail::wait(_fd, detail::wait_for::read, timeout);
    if (!wait_r) {
        return error(wait_r.error());
    }
    return try_accept();
}

} // namespace idfxx::net
