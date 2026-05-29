// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/datagram_channel>

#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/opt.h>

// Verify UDP protocol tags match lwIP.
static_assert(NETCONN_UDP == 0x20);
static_assert(NETCONN_UDPLITE == 0x21);
static_assert(NETCONN_UDPNOCHKSUM == 0x22);
#ifdef CONFIG_LWIP_IPV6
static_assert((NETCONN_UDP | NETCONN_TYPE_IPV6) == NETCONN_UDP_IPV6);
#endif

namespace idfxx::net::netconn {

namespace {

int udp_variant_to_proto(udp_variant variant) {
    switch (variant) {
    case udp_variant::lite:
        return NETCONN_UDPLITE;
    case udp_variant::no_checksum:
        return NETCONN_UDPNOCHKSUM;
    case udp_variant::standard:
    default:
        return NETCONN_UDP;
    }
}

} // namespace

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
datagram_channel::datagram_channel()
    : datagram_channel(idfxx::unwrap(make())) {}

datagram_channel::datagram_channel(address_family fam, udp_variant variant)
    : datagram_channel(idfxx::unwrap(make(fam, variant))) {}
#endif

result<datagram_channel> datagram_channel::make() {
    return make(address_family::ipv4, udp_variant::standard);
}

result<datagram_channel> datagram_channel::make(address_family fam, udp_variant variant) {
    auto conn = detail::make_netconn(udp_variant_to_proto(variant), fam);
    if (!conn) {
        return error(conn.error());
    }
    return datagram_channel(*conn, fam);
}

namespace {

result<void>
ipv4_membership(::netconn* conn, address_family fam, ipv4_addr group, ipv4_addr interface, enum netconn_igmp op) {
    if (conn == nullptr) {
        return error(errc::invalid_state);
    }
    if (fam != address_family::ipv4) {
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

result<void> datagram_channel::try_join_multicast_v4(ipv4_addr group, ipv4_addr interface) {
    return ipv4_membership(_conn, _family, group, interface, NETCONN_JOIN);
}

result<void> datagram_channel::try_leave_multicast_v4(ipv4_addr group, ipv4_addr interface) {
    return ipv4_membership(_conn, _family, group, interface, NETCONN_LEAVE);
}

} // namespace idfxx::net::netconn
