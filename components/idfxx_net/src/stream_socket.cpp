// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/select.hpp"
#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"

#include <idfxx/net/stream_socket>

#include <errno.h>
#include <fcntl.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <optional>
#include <tuple>
#include <utility>

namespace idfxx::net {

namespace {

result<void> apply_config(stream_socket& s, const stream_socket::config& cfg) {
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
    if (cfg.reuse_address) {
        if (auto r = detail::set_int_option(fd, SOL_SOCKET, SO_REUSEADDR, 1); !r) {
            return r;
        }
    }
    if (cfg.no_delay) {
        if (auto r = detail::set_int_option(fd, IPPROTO_TCP, TCP_NODELAY, 1); !r) {
            return r;
        }
    }
    if (cfg.keep_alive) {
        if (auto r = detail::set_int_option(fd, SOL_SOCKET, SO_KEEPALIVE, 1); !r) {
            return r;
        }
    }
    if (cfg.keep_idle) {
        if (auto r = detail::set_int_option(fd, IPPROTO_TCP, TCP_KEEPIDLE, static_cast<int>(cfg.keep_idle->count()));
            !r) {
            return r;
        }
    }
    if (cfg.keep_interval) {
        if (auto r =
                detail::set_int_option(fd, IPPROTO_TCP, TCP_KEEPINTVL, static_cast<int>(cfg.keep_interval->count()));
            !r) {
            return r;
        }
    }
    if (cfg.keep_count) {
        if (auto r = detail::set_int_option(fd, IPPROTO_TCP, TCP_KEEPCNT, *cfg.keep_count); !r) {
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
stream_socket::stream_socket()
    : stream_socket(idfxx::unwrap(make())) {}

stream_socket::stream_socket(enum family fam)
    : stream_socket(idfxx::unwrap(make(fam))) {}

stream_socket::stream_socket(const config& cfg)
    : stream_socket(idfxx::unwrap(make(cfg))) {}

stream_socket::stream_socket(const endpoint& peer)
    : stream_socket(idfxx::unwrap(connect_to(peer))) {}

stream_socket::stream_socket(const endpoint& peer, const config& cfg)
    : stream_socket(idfxx::unwrap(connect_to(peer, cfg))) {}
#endif

result<stream_socket> stream_socket::make() {
    return make(config{});
}

result<stream_socket> stream_socket::make(enum family fam) {
    return make(config{.family = fam});
}

result<stream_socket> stream_socket::make(const config& cfg) {
    int af = detail::family_to_af(cfg.family);
    if (af == AF_UNSPEC) {
        return error(errc::invalid_argument);
    }
    int fd = lwip_socket(af, SOCK_STREAM, 0);
    if (fd < 0) {
        return error(errno_to_error_code(errno));
    }
    stream_socket s(fd, cfg.family);
    if (auto r = apply_config(s, cfg); !r) {
        return error(r.error());
    }
    return s;
}

result<stream_socket> stream_socket::connect_to(const endpoint& peer) {
    auto fam = peer.family();
    if (!fam) {
        return error(errc::invalid_argument);
    }
    return connect_to(peer, config{.family = *fam});
}

result<stream_socket> stream_socket::connect_to(const endpoint& peer, const config& cfg) {
    auto fam = peer.family();
    if (!fam) {
        return error(errc::invalid_argument);
    }
    config c = cfg;
    c.family = *fam;
    auto sock = make(c);
    if (!sock) {
        return error(sock.error());
    }
    if (auto r = sock->try_connect(peer); !r) {
        return error(r.error());
    }
    return std::move(*sock);
}

result<void> stream_socket::_try_connect_for(const endpoint& peer, std::chrono::milliseconds timeout) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }

    int saved_flags = lwip_fcntl(_fd, F_GETFL, 0);
    if (saved_flags < 0) {
        return error(errno_to_error_code(errno));
    }
    bool was_non_blocking = (saved_flags & O_NONBLOCK) != 0;

    if (!was_non_blocking) {
        if (lwip_fcntl(_fd, F_SETFL, saved_flags | O_NONBLOCK) < 0) {
            return error(errno_to_error_code(errno));
        }
    }

    auto restore_flags = [&]() {
        if (!was_non_blocking) {
            lwip_fcntl(_fd, F_SETFL, saved_flags);
        }
    };

    sockaddr_storage ss{};
    auto len = detail::to_sockaddr(peer, ss);
    if (len == 0) {
        restore_flags();
        return error(errc::invalid_argument);
    }

    int rc = lwip_connect(_fd, reinterpret_cast<const sockaddr*>(&ss), len);
    if (rc == 0) {
        restore_flags();
        return {};
    }
    if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        auto ec = errno_to_error_code(errno);
        restore_flags();
        return error(ec);
    }

    auto wait_r = detail::wait(_fd, detail::wait_for::write | detail::wait_for::except, timeout);
    if (!wait_r) {
        restore_flags();
        return error(wait_r.error());
    }

    int so_err = get_last_error();
    restore_flags();
    if (so_err != 0) {
        return error(errno_to_error_code(so_err));
    }
    return {};
}

result<size_t> stream_socket::try_send(std::span<const std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    ssize_t n = lwip_send(_fd, buf.data(), buf.size(), 0);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    return static_cast<size_t>(n);
}

result<size_t> stream_socket::try_send(std::string_view buf) {
    return try_send(std::as_bytes(std::span<const char>(buf.data(), buf.size())));
}

result<void> stream_socket::try_send_all(std::span<const std::byte> buf) {
    while (!buf.empty()) {
        auto r = try_send(buf);
        if (!r) {
            return error(r.error());
        }
        if (*r == 0) {
            return error(errc::netconn_closed);
        }
        buf = buf.subspan(*r);
    }
    return {};
}

result<void> stream_socket::try_recv_exact(std::span<std::byte> buf) {
    while (!buf.empty()) {
        auto r = try_recv(buf);
        if (!r) {
            return error(r.error());
        }
        if (r->empty()) {
            return error(errc::netconn_closed);
        }
        buf = buf.subspan(r->size());
    }
    return {};
}

result<void> stream_socket::try_shutdown() {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    if (lwip_shutdown(_fd, SHUT_RDWR) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<void> stream_socket::try_shutdown(direction dir) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    int how = (dir == direction::receive) ? SHUT_RD : SHUT_WR;
    if (lwip_shutdown(_fd, how) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

void stream_socket::set_no_delay(bool on) noexcept {
    std::ignore = detail::set_int_option(_fd, IPPROTO_TCP, TCP_NODELAY, on ? 1 : 0);
}

void stream_socket::set_keep_alive(bool on) noexcept {
    std::ignore = detail::set_int_option(_fd, SOL_SOCKET, SO_KEEPALIVE, on ? 1 : 0);
}

void stream_socket::_set_keep_idle(std::chrono::seconds s) noexcept {
    std::ignore = detail::set_int_option(_fd, IPPROTO_TCP, TCP_KEEPIDLE, static_cast<int>(s.count()));
}

void stream_socket::_set_keep_interval(std::chrono::seconds s) noexcept {
    std::ignore = detail::set_int_option(_fd, IPPROTO_TCP, TCP_KEEPINTVL, static_cast<int>(s.count()));
}

void stream_socket::set_keep_count(uint32_t count) noexcept {
    std::ignore = detail::set_int_option(_fd, IPPROTO_TCP, TCP_KEEPCNT, static_cast<int>(count));
}

bool stream_socket::get_no_delay() const noexcept {
    if (_fd < 0) {
        return false;
    }
    auto r = detail::get_int_option(_fd, IPPROTO_TCP, TCP_NODELAY);
    return r && *r != 0;
}

bool stream_socket::get_keep_alive() const noexcept {
    if (_fd < 0) {
        return false;
    }
    auto r = detail::get_int_option(_fd, SOL_SOCKET, SO_KEEPALIVE);
    return r && *r != 0;
}

} // namespace idfxx::net
