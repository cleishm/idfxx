// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/raw_connection>

#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/netbuf.h>
#include <lwip/opt.h>
#include <utility>

// Verify the protocol tag for raw and raw+IPv6 matches lwIP.
static_assert(NETCONN_RAW == 0x40);
#ifdef CONFIG_LWIP_IPV6
static_assert((NETCONN_RAW | NETCONN_TYPE_IPV6) == NETCONN_RAW_IPV6);
#endif

namespace idfxx::net::netconn {

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
raw_connection::raw_connection(int protocol)
    : raw_connection(idfxx::unwrap(make(protocol))) {}

raw_connection::raw_connection(int protocol, enum family fam)
    : raw_connection(idfxx::unwrap(make(protocol, fam))) {}
#endif

result<raw_connection> raw_connection::make(int protocol) {
    return make(protocol, family::ipv4);
}

result<raw_connection> raw_connection::make(int protocol, enum family fam) {
    auto conn = detail::make_netconn_with_proto(NETCONN_RAW, static_cast<uint8_t>(protocol), fam);
    if (!conn) {
        return error(conn.error());
    }
    return raw_connection(*conn, fam);
}

result<void> raw_connection::try_connect(const endpoint& peer) {
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

result<void> raw_connection::try_disconnect() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_disconnect(_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> raw_connection::try_send(netbuf& buf) {
    if (_conn == nullptr || buf.idf_handle() == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_send(_conn, buf.idf_handle());
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> raw_connection::try_send_to(netbuf& buf, const endpoint& to) {
    if (_conn == nullptr || buf.idf_handle() == nullptr) {
        return error(errc::invalid_state);
    }
    auto bound = net::detail::endpoint_to_ip_addr(to);
    if (!bound) {
        return error(errc::invalid_argument);
    }
    auto& [ip, port] = *bound;
    auto rc = ::netconn_sendto(_conn, buf.idf_handle(), &ip, port);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> raw_connection::try_send(std::span<const std::byte> data) {
    auto nb = netbuf::make();
    if (!nb) {
        return error(nb.error());
    }
    auto attach = nb->try_attach(data);
    if (!attach) {
        return error(attach.error());
    }
    return try_send(*nb);
}

result<void> raw_connection::try_send_to(std::span<const std::byte> data, const endpoint& to) {
    auto nb = netbuf::make();
    if (!nb) {
        return error(nb.error());
    }
    auto attach = nb->try_attach(data);
    if (!attach) {
        return error(attach.error());
    }
    return try_send_to(*nb, to);
}

result<netbuf> raw_connection::try_recv() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    ::netbuf* buf = nullptr;
    auto rc = ::netconn_recv(_conn, &buf);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return netbuf(buf);
}

result<std::span<std::byte>> raw_connection::try_recv(std::span<std::byte> buf) {
    auto nb = try_recv();
    if (!nb) {
        return error(nb.error());
    }
    return buf.first(nb->copy_into(buf));
}

result<raw_connection::datagram> raw_connection::try_recv_from(std::span<std::byte> buf) {
    auto nb = try_recv();
    if (!nb) {
        return error(nb.error());
    }
    auto bytes = nb->copy_into(buf);
    auto from = nb->from_endpoint();
    if (!from) {
        return error(errc::invalid_state);
    }
    return datagram{buf.first(bytes), *from};
}

} // namespace idfxx::net::netconn
