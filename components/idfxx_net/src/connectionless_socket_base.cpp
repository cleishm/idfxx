// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockaddr.hpp"

#include <idfxx/net/detail/connectionless_socket_base.hpp>

#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>

namespace idfxx::net::detail {

result<void> connectionless_socket_base::try_disconnect() {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    ss.ss_family = AF_UNSPEC;
    if (lwip_connect(_fd, reinterpret_cast<const sockaddr*>(&ss), sizeof(ss)) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<void> connectionless_socket_base::try_send_to(std::span<const std::byte> buf, const endpoint& to) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    auto len = to_sockaddr(to, ss);
    if (len == 0) {
        return error(errc::invalid_argument);
    }
    ssize_t n = lwip_sendto(_fd, buf.data(), buf.size(), 0, reinterpret_cast<const sockaddr*>(&ss), len);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<datagram> connectionless_socket_base::try_recv_from(std::span<std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    ssize_t n = lwip_recvfrom(_fd, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&ss), &len);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    auto ep = from_sockaddr(reinterpret_cast<const sockaddr*>(&ss), len);
    if (!ep) {
        return error(errc::not_supported);
    }
    return datagram{buf.first(static_cast<size_t>(n)), *ep};
}

} // namespace idfxx::net::detail
