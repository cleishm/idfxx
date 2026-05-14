// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"

#include <idfxx/net/raw_socket>

#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <utility>

namespace idfxx::net {

namespace {

result<void> apply_config(raw_socket& s, const raw_socket::config& cfg) {
    int fd = s.idf_handle();
    if (cfg.non_blocking) {
        if (auto r = detail::set_non_blocking(fd, true); !r) {
            return r;
        }
    }
    if (cfg.recv_timeout) {
        if (auto r = detail::set_timeout(fd, SO_RCVTIMEO, *cfg.recv_timeout); !r) {
            return r;
        }
    }
    if (cfg.send_timeout) {
        if (auto r = detail::set_timeout(fd, SO_SNDTIMEO, *cfg.send_timeout); !r) {
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
raw_socket::raw_socket(int protocol)
    : raw_socket(idfxx::unwrap(make(protocol))) {}

raw_socket::raw_socket(int protocol, enum family fam)
    : raw_socket(idfxx::unwrap(make(protocol, fam))) {}

raw_socket::raw_socket(const config& cfg)
    : raw_socket(idfxx::unwrap(make(cfg))) {}
#endif

result<raw_socket> raw_socket::make(int protocol) {
    return make(config{.protocol = protocol});
}

result<raw_socket> raw_socket::make(int protocol, enum family fam) {
    return make(config{.protocol = protocol, .family = fam});
}

result<raw_socket> raw_socket::make(const config& cfg) {
    int af = detail::family_to_af(cfg.family);
    if (af == AF_UNSPEC) {
        return error(errc::invalid_argument);
    }
    int fd = lwip_socket(af, SOCK_RAW, cfg.protocol);
    if (fd < 0) {
        return error(errno_to_error_code(errno));
    }
    raw_socket s(raw_socket::from_fd_t{}, fd, cfg.family);
    if (auto r = apply_config(s, cfg); !r) {
        return error(r.error());
    }
    return s;
}

result<void> raw_socket::try_send(std::span<const std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    ssize_t n = lwip_send(_fd, buf.data(), buf.size(), 0);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

} // namespace idfxx::net
