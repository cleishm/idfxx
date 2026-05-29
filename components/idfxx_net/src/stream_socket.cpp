// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/select.hpp"
#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"
#include "detail/sockopt_apply.hpp"

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
    return {};
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
stream_socket::stream_socket()
    : stream_socket(idfxx::unwrap(make())) {}

stream_socket::stream_socket(address_family fam)
    : stream_socket(idfxx::unwrap(make(fam))) {}

stream_socket::stream_socket(const config& cfg)
    : stream_socket(idfxx::unwrap(make(cfg))) {}

stream_socket::stream_socket(const endpoint& peer)
    : stream_socket(idfxx::unwrap(connect_to(peer))) {}

stream_socket::stream_socket(const endpoint& peer, const config& cfg)
    : stream_socket(idfxx::unwrap(connect_to(peer, cfg))) {}

stream_socket::stream_socket(std::string_view host, port_number port)
    : stream_socket(idfxx::unwrap(connect_to(host, port))) {}

stream_socket::stream_socket(std::string_view host, port_number port, const config& cfg, const resolver_options& opts)
    : stream_socket(idfxx::unwrap(connect_to(host, port, cfg, opts))) {}
#endif

result<stream_socket> stream_socket::make() {
    return make(config{});
}

result<stream_socket> stream_socket::make(address_family fam) {
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
    return connect_to(peer, config{.family = peer.family()});
}

result<stream_socket> stream_socket::connect_to(const endpoint& peer, const config& cfg) {
    config c = cfg;
    c.family = peer.family();
    auto sock = make(c);
    if (!sock) {
        return error(sock.error());
    }
    if (auto r = sock->try_connect(peer); !r) {
        return error(r.error());
    }
    return std::move(*sock);
}

namespace {

// Walks the resolved candidates and attempts @p attempt on each. Returns the
// first success, or the last attempt error if every candidate failed, or
// `errc::name_not_found` if resolution succeeded but yielded no candidates.
template<typename Service, typename Attempt>
result<stream_socket>
connect_to_impl(std::string_view host, Service service, const resolver_options& opts, Attempt&& attempt) {
    std::optional<stream_socket> connected;
    bool any_tried = false;
    std::error_code last_err{};
    auto resolve_r = try_resolve_each(
        host,
        service,
        [&](const endpoint& ep) {
            any_tried = true;
            auto sock_r = attempt(ep);
            if (sock_r) {
                connected.emplace(std::move(*sock_r));
                return false; // stop iteration
            }
            last_err = sock_r.error();
            return true; // try next candidate
        },
        opts
    );
    if (!resolve_r) {
        return error(resolve_r.error());
    }
    if (connected) {
        return std::move(*connected);
    }
    if (!any_tried) {
        return error(errc::name_not_found);
    }
    return error(last_err);
}

} // namespace

result<stream_socket> stream_socket::connect_to(std::string_view host, port_number port) {
    return connect_to(host, port, config{}, {});
}

result<stream_socket>
stream_socket::connect_to(std::string_view host, port_number port, const config& cfg, const resolver_options& opts) {
    return connect_to_impl(host, port, opts, [&](const endpoint& ep) { return connect_to(ep, cfg); });
}

result<stream_socket>
stream_socket::_make_and_connect_for(const endpoint& ep, const config& cfg, std::chrono::milliseconds timeout) {
    config c = cfg;
    c.family = ep.family();
    auto sock_r = make(c);
    if (!sock_r) {
        return error(sock_r.error());
    }
    if (auto r = sock_r->_try_connect_for(ep, timeout); !r) {
        return error(r.error());
    }
    return std::move(*sock_r);
}

result<stream_socket> stream_socket::_connect_to_for(
    std::string_view host,
    port_number port,
    std::chrono::milliseconds timeout,
    const config& cfg,
    const resolver_options& opts
) {
    return connect_to_impl(host, port, opts, [&](const endpoint& ep) {
        return _make_and_connect_for(ep, cfg, timeout);
    });
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

    auto so_err = get_last_error();
    restore_flags();
    if (so_err) {
        return error(so_err);
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
        buf = buf.subspan(*r);
    }
    return {};
}

result<void> stream_socket::try_send_all(std::string_view buf) {
    return try_send_all(std::as_bytes(std::span<const char>(buf.data(), buf.size())));
}

result<void> stream_socket::try_recv_exact(std::span<std::byte> buf) {
    while (!buf.empty()) {
        auto r = try_recv(buf);
        if (!r) {
            return error(r.error());
        }
        if (r->empty()) {
            return error(errc::unexpected_eof);
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
    if (_fd < 0) {
        return;
    }
    std::ignore = detail::set_int_option(_fd, IPPROTO_TCP, TCP_NODELAY, on ? 1 : 0);
}

void stream_socket::set_keep_alive(bool on) noexcept {
    if (_fd < 0) {
        return;
    }
    std::ignore = detail::set_int_option(_fd, SOL_SOCKET, SO_KEEPALIVE, on ? 1 : 0);
}

void stream_socket::_set_keep_idle(std::chrono::seconds s) noexcept {
    if (_fd < 0) {
        return;
    }
    std::ignore = detail::set_int_option(_fd, IPPROTO_TCP, TCP_KEEPIDLE, static_cast<int>(s.count()));
}

void stream_socket::_set_keep_interval(std::chrono::seconds s) noexcept {
    if (_fd < 0) {
        return;
    }
    std::ignore = detail::set_int_option(_fd, IPPROTO_TCP, TCP_KEEPINTVL, static_cast<int>(s.count()));
}

void stream_socket::set_keep_count(uint32_t count) noexcept {
    if (_fd < 0) {
        return;
    }
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
