// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/detail/connectionless_channel.hpp>

#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/netbuf.h>
#include <lwip/opt.h>

namespace idfxx::net::netconn::detail {

result<void> connectionless_channel::try_connect(const endpoint& peer) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto bound = net::detail::endpoint_to_ip_addr(peer);
    if (!bound) {
        return error(errc::invalid_argument);
    }
    auto& [ip, port] = *bound;
    auto rc = ::netconn_connect(_conn, &ip, port);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> connectionless_channel::try_disconnect() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_disconnect(_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<size_t> connectionless_channel::try_send(buffer& buf) {
    if (_conn == nullptr || buf.idf_handle() == nullptr) {
        return error(errc::invalid_state);
    }
    auto sent = buf.len();
    auto rc = ::netconn_send(_conn, buf.idf_handle());
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return sent;
}

result<size_t> connectionless_channel::try_send_to(buffer& buf, const endpoint& to) {
    if (_conn == nullptr || buf.idf_handle() == nullptr) {
        return error(errc::invalid_state);
    }
    auto bound = net::detail::endpoint_to_ip_addr(to);
    if (!bound) {
        return error(errc::invalid_argument);
    }
    auto& [ip, port] = *bound;
    auto sent = buf.len();
    auto rc = ::netconn_sendto(_conn, buf.idf_handle(), &ip, port);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return sent;
}

result<size_t> connectionless_channel::try_send(std::span<const std::byte> data) {
    auto nb = buffer::make();
    if (!nb) {
        return error(nb.error());
    }
    auto attach = nb->try_attach(data);
    if (!attach) {
        return error(attach.error());
    }
    return try_send(*nb);
}

result<size_t> connectionless_channel::try_send_to(std::span<const std::byte> data, const endpoint& to) {
    auto nb = buffer::make();
    if (!nb) {
        return error(nb.error());
    }
    auto attach = nb->try_attach(data);
    if (!attach) {
        return error(attach.error());
    }
    return try_send_to(*nb, to);
}

result<buffer> connectionless_channel::try_recv() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    ::netbuf* buf = nullptr;
    auto rc = ::netconn_recv(_conn, &buf);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return buffer(buf);
}

result<std::span<std::byte>> connectionless_channel::try_recv(std::span<std::byte> buf) {
    auto nb = try_recv();
    if (!nb) {
        return error(nb.error());
    }
    // A packet is delivered whole or not at all: an overrun is reported rather
    // than silently truncated, matching the BSD `connectionless_socket_base`.
    if (nb->len() > buf.size()) {
        return error(errc::message_too_long);
    }
    return buf.first(nb->copy_into(buf));
}

result<connectionless_channel::datagram> connectionless_channel::try_recv_from(std::span<std::byte> buf) {
    auto nb = try_recv();
    if (!nb) {
        return error(nb.error());
    }
    auto bytes = nb->copy_into(buf);
    auto from = nb->from_endpoint();
    if (!from) {
        return error(errc::invalid_state);
    }
    return datagram{buf.first(bytes), *from, nb->len() > buf.size()};
}

} // namespace idfxx::net::netconn::detail
