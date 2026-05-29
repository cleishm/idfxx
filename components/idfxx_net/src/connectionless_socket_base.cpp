// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockaddr.hpp"

#include <idfxx/net/detail/connectionless_socket_base.hpp>

#include <algorithm>
#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>

namespace idfxx::net::detail {

result<std::span<std::byte>> connectionless_socket_base::try_recv(std::span<std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    iovec iov{};
    iov.iov_base = buf.data();
    iov.iov_len = buf.size();
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    // recvmsg raises MSG_TRUNC when the datagram overruns the buffer; a plain
    // recv silently caps at the buffer size and cannot surface it. The datagram
    // is consumed either way (datagrams cannot be read in pieces), so an
    // overrun is reported as an error rather than a silent truncation.
    ssize_t len = lwip_recvmsg(_fd, &msg, 0);
    if (len < 0) {
        return error(errno_to_error_code(errno));
    }
    if ((msg.msg_flags & MSG_TRUNC) != 0) {
        return error(errc::message_too_long);
    }
    return buf.first(static_cast<size_t>(len));
}

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

result<size_t> connectionless_socket_base::try_send_to(std::span<const std::byte> buf, const endpoint& to) {
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
    return static_cast<size_t>(n);
}

result<datagram> connectionless_socket_base::try_recv_from(std::span<std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    iovec iov{};
    iov.iov_base = buf.data();
    iov.iov_len = buf.size();
    msghdr msg{};
    msg.msg_name = &ss;
    msg.msg_namelen = sizeof(ss);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    // recvmsg reports the datagram's true length (not just the bytes copied)
    // and raises MSG_TRUNC in msg_flags when it overflows the buffer. A plain
    // recvfrom caps its return at the buffer size and cannot surface either,
    // so recvmsg is the only way to detect an oversized datagram — and it does
    // so in a single call.
    ssize_t len = lwip_recvmsg(_fd, &msg, 0);
    if (len < 0) {
        return error(errno_to_error_code(errno));
    }
    auto ep = from_sockaddr(reinterpret_cast<const sockaddr*>(&ss), msg.msg_namelen);
    if (!ep) {
        return error(errc::not_supported);
    }
    auto copied = std::min(static_cast<size_t>(len), buf.size());
    bool truncated = (msg.msg_flags & MSG_TRUNC) != 0;
    return datagram{buf.first(copied), *ep, truncated};
}

} // namespace idfxx::net::detail
