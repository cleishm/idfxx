// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/error.hpp"
#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"

#include <idfxx/net/detail/connectionless_socket_base.hpp>

#include <algorithm>
#include <errno.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>

namespace idfxx::net::detail {

result<byte_span> connectionless_socket_base::try_recv(byte_span buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    iovec iov{};
    iov.iov_base = buf.data();
    iov.iov_len = buf.size();
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    // recvmsg sets MSG_TRUNC when the datagram is larger than the buffer. A plain
    // recv silently stops at the buffer size and cannot report the truncation.
    // The datagram is consumed in both cases (datagrams cannot be read in
    // pieces), so an oversized datagram is reported as an error rather than
    // silently truncated.
    ssize_t len = lwip_recvmsg(_fd, &msg, 0);
    if (len < 0) {
        return error(io_error_code(_fd, errno));
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

result<size_t> connectionless_socket_base::try_send_to(byte_view buf, const endpoint& to) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    auto len = to_sockaddr(to, ss);
    if (len == 0) {
        return error(errc::invalid_argument);
    }
    ssize_t n =
        lwip_sendto(_fd, buf.data(), buf.size(), 0, reinterpret_cast<const sockaddr*>(&ss), len);
    if (n < 0) {
        return error(io_error_code(_fd, errno));
    }
    return static_cast<size_t>(n);
}

result<datagram> connectionless_socket_base::try_recv_from(byte_span buf) {
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
    // recvmsg reports the datagram's true length (not just the bytes copied).
    // It also sets MSG_TRUNC in msg_flags when the datagram is larger than the
    // buffer. A plain recvfrom limits its return value to the buffer size and
    // cannot report either fact. So recvmsg is the only way to detect an
    // oversized datagram, and it does so in a single call.
    ssize_t len = lwip_recvmsg(_fd, &msg, 0);
    if (len < 0) {
        return error(io_error_code(_fd, errno));
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
