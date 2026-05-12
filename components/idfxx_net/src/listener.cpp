// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/select.hpp"
#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"

#include <idfxx/net/listener>

#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <utility>

namespace idfxx::net {

namespace {

result<void> apply_listener_config(listener& a, const listener::config& cfg) {
    int fd = a.idf_handle();
    if (cfg.reuse_address) {
        if (auto r = detail::set_int_option(fd, SOL_SOCKET, SO_REUSEADDR, 1); !r) {
            return r;
        }
    }
    if (cfg.non_blocking) {
        if (auto r = detail::set_non_blocking(fd, true); !r) {
            return r;
        }
    }
#ifdef CONFIG_LWIP_IPV6
    if (cfg.ipv6_only && cfg.family == family::ipv6) {
        if (auto r = detail::set_ipv6_only(fd, true); !r) {
            return r;
        }
    }
#endif
    if (cfg.bind_to_device) {
        if (auto r = detail::set_bind_to_device(fd, *cfg.bind_to_device); !r) {
            return r;
        }
    }
    return {};
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
    if (!fam) {
        return error(errc::invalid_argument);
    }

    int af = detail::family_to_af(*fam);
    int fd = lwip_socket(af, SOCK_STREAM, 0);
    if (fd < 0) {
        return error(errno_to_error_code(errno));
    }
    listener a(fd, *fam);

    config c = cfg;
    c.family = *fam;
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
    return make(port, {.family = family::ipv6});
#else
    return make(port, {.family = family::ipv4});
#endif
}

result<listener> listener::make(port_number port, const config& cfg) {
    if (cfg.family == family::ipv6) {
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

result<std::pair<stream_socket, endpoint>> listener::try_accept_with_peer() {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    int client_fd = lwip_accept(_fd, reinterpret_cast<sockaddr*>(&ss), &len);
    if (client_fd < 0) {
        return error(errno_to_error_code(errno));
    }
    auto peer = detail::from_sockaddr(reinterpret_cast<const sockaddr*>(&ss), len);
    return std::pair<stream_socket, endpoint>{stream_socket(client_fd, _family), peer.value_or(endpoint{})};
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
