// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/netconn/raw_channel>

#include <lwip/api.h>
#include <lwip/opt.h>

// Verify the protocol tag for raw and raw+IPv6 matches lwIP.
static_assert(NETCONN_RAW == 0x40);
#ifdef CONFIG_LWIP_IPV6
static_assert((NETCONN_RAW | NETCONN_TYPE_IPV6) == NETCONN_RAW_IPV6);
#endif

namespace idfxx::net::netconn {

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
raw_channel::raw_channel(ip_protocol protocol)
    : raw_channel(idfxx::unwrap(make(protocol))) {}

raw_channel::raw_channel(ip_protocol protocol, address_family fam)
    : raw_channel(idfxx::unwrap(make(protocol, fam))) {}
#endif

result<raw_channel> raw_channel::make(ip_protocol protocol) {
    return make(protocol, address_family::ipv4);
}

result<raw_channel> raw_channel::make(ip_protocol protocol, address_family fam) {
    auto conn = detail::make_netconn_with_proto(NETCONN_RAW, static_cast<uint8_t>(protocol), fam);
    if (!conn) {
        return error(conn.error());
    }
    return raw_channel(*conn, fam);
}

} // namespace idfxx::net::netconn
