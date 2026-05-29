// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/select.hpp"
#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"

#include <idfxx/net/detail/ip_io_socket_base.hpp>

#include <algorithm>
#include <errno.h>
#include <limits>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <tuple>

namespace idfxx::net::detail {

result<endpoint> ip_io_socket_base::try_peer_endpoint() const {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    return get_peer_endpoint(_fd);
}

result<void> ip_io_socket_base::try_connect(const endpoint& peer) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    auto len = to_sockaddr(peer, ss);
    if (len == 0) {
        return error(errc::invalid_argument);
    }
    if (lwip_connect(_fd, reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<std::span<std::byte>> ip_io_socket_base::try_recv(std::span<std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    ssize_t n = lwip_recv(_fd, buf.data(), buf.size(), 0);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    return buf.first(static_cast<size_t>(n));
}

void ip_io_socket_base::set_send_buffer(size_t bytes) noexcept {
    if (_fd < 0) {
        return;
    }
    auto clamped = std::min<size_t>(bytes, std::numeric_limits<int>::max());
    std::ignore = set_int_option(_fd, SOL_SOCKET, SO_SNDBUF, static_cast<int>(clamped));
}

void ip_io_socket_base::_set_send_timeout(std::chrono::milliseconds t) noexcept {
    if (_fd < 0) {
        return;
    }
    std::ignore = set_timeout(_fd, SO_SNDTIMEO, t);
}

result<void> ip_io_socket_base::_try_wait_writable(std::chrono::milliseconds timeout) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    return wait(_fd, wait_for::write, timeout);
}

size_t ip_io_socket_base::get_send_buffer() const noexcept {
    if (_fd < 0) {
        return 0;
    }
    auto r = get_int_option(_fd, SOL_SOCKET, SO_SNDBUF);
    return r ? static_cast<size_t>(*r) : 0;
}

} // namespace idfxx::net::detail
