// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/datagram_connection>

#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/netbuf.h>
#include <lwip/opt.h>
#include <utility>

// Verify UDP protocol tags match lwIP.
static_assert(NETCONN_UDP == 0x20);
static_assert(NETCONN_UDPLITE == 0x21);
static_assert(NETCONN_UDPNOCHKSUM == 0x22);
#ifdef CONFIG_LWIP_IPV6
static_assert((NETCONN_UDP | NETCONN_TYPE_IPV6) == NETCONN_UDP_IPV6);
#endif

namespace idfxx::net::netconn {

namespace {

int udp_mode_to_proto(udp_mode mode) {
    switch (mode) {
    case udp_mode::udp_lite:
        return NETCONN_UDPLITE;
    case udp_mode::udp_no_chksum:
        return NETCONN_UDPNOCHKSUM;
    case udp_mode::udp:
    default:
        return NETCONN_UDP;
    }
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
datagram_connection::datagram_connection()
    : datagram_connection(idfxx::unwrap(make())) {}

datagram_connection::datagram_connection(enum family fam, enum udp_mode mode)
    : datagram_connection(idfxx::unwrap(make(fam, mode))) {}
#endif

result<datagram_connection> datagram_connection::make() {
    return make(family::ipv4, udp_mode::udp);
}

result<datagram_connection> datagram_connection::make(enum family fam, enum udp_mode mode) {
    auto conn = detail::make_netconn(udp_mode_to_proto(mode), fam);
    if (!conn) {
        return error(conn.error());
    }
    return datagram_connection(*conn, fam);
}

result<void> datagram_connection::try_connect(const endpoint& peer) {
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

result<void> datagram_connection::try_disconnect() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_disconnect(_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> datagram_connection::try_send(netbuf& buf) {
    if (_conn == nullptr || buf.idf_handle() == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_send(_conn, buf.idf_handle());
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> datagram_connection::try_send_to(netbuf& buf, const endpoint& to) {
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

result<void> datagram_connection::try_send(std::span<const std::byte> data) {
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

result<void> datagram_connection::try_send_to(std::span<const std::byte> data, const endpoint& to) {
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

result<netbuf> datagram_connection::try_recv() {
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

result<std::span<std::byte>> datagram_connection::try_recv(std::span<std::byte> buf) {
    auto nb = try_recv();
    if (!nb) {
        return error(nb.error());
    }
    return buf.first(nb->copy_into(buf));
}

result<datagram_connection::datagram> datagram_connection::try_recv_from(std::span<std::byte> buf) {
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

namespace {

result<void>
ipv4_membership(::netconn* conn, enum family fam, ipv4_addr group, ipv4_addr interface, enum netconn_igmp op) {
    if (conn == nullptr) {
        return error(errc::invalid_state);
    }
    if (fam != family::ipv4) {
        return error(errc::wrong_protocol_type);
    }
    ::ip_addr_t g{};
    ::ip_addr_t ifa{};
    net::detail::ipv4_to_ip_addr(group, g);
    net::detail::ipv4_to_ip_addr(interface, ifa);
    auto rc = ::netconn_join_leave_group(conn, &g, &ifa, op);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

} // namespace

result<void> datagram_connection::try_join_multicast_v4(ipv4_addr group, ipv4_addr interface) {
    return ipv4_membership(_conn, _family, group, interface, NETCONN_JOIN);
}

result<void> datagram_connection::try_leave_multicast_v4(ipv4_addr group, ipv4_addr interface) {
    return ipv4_membership(_conn, _family, group, interface, NETCONN_LEAVE);
}

} // namespace idfxx::net::netconn
