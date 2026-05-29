// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/listener>

#include <cstdint>
#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/opt.h>
#include <utility>

namespace idfxx::net::netconn {

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
listener::listener()
    : listener(idfxx::unwrap(make())) {}

listener::listener(address_family fam)
    : listener(idfxx::unwrap(make(fam))) {}

listener::listener(const endpoint& bind_addr, int backlog)
    : listener(idfxx::unwrap(make(bind_addr, backlog))) {}

listener::listener(port_number port, int backlog)
    : listener(idfxx::unwrap(make(port, backlog))) {}
#endif

result<listener> listener::make() {
    return make(address_family::ipv4);
}

result<listener> listener::make(address_family fam) {
    auto conn = detail::make_netconn(NETCONN_TCP, fam);
    if (!conn) {
        return error(conn.error());
    }
    return listener(*conn, fam);
}

result<listener> listener::make(const endpoint& bind_addr, int backlog) {
    auto l = make(bind_addr.family());
    if (!l) {
        return error(l.error());
    }
    if (auto r = l->try_bind(bind_addr); !r) {
        return error(r.error());
    }
    if (auto r = l->try_listen(backlog); !r) {
        return error(r.error());
    }
    return std::move(*l);
}

result<listener> listener::make(port_number port, int backlog) {
#ifdef CONFIG_LWIP_IPV6
    return make(endpoint{ipv6_addr::any(), port}, backlog);
#else
    return make(endpoint{ipv4_addr::any(), port}, backlog);
#endif
}

result<void> listener::try_listen(int backlog) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_listen_with_backlog(_conn, static_cast<uint8_t>(backlog));
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<stream_channel> listener::try_accept() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    ::netconn* new_conn = nullptr;
    auto rc = ::netconn_accept(_conn, &new_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return stream_channel(new_conn, _family);
}

result<listener::accepted_channel> listener::try_accept_with_peer() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    ::netconn* new_conn = nullptr;
    auto rc = ::netconn_accept(_conn, &new_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    // Take ownership of the accepted netconn immediately so it is released if
    // the (near-impossible) peer-address lookup below fails.
    stream_channel channel(new_conn, _family);
    ::ip_addr_t ip{};
    uint16_t port = 0;
    auto addr_rc = ::netconn_getaddr(new_conn, &ip, &port, 0 /* peer */);
    if (addr_rc != ERR_OK) {
        return error(lwip_err_to_error_code(addr_rc));
    }
    auto peer = net::detail::ip_addr_to_endpoint(&ip, port);
    if (!peer) {
        return error(errc::invalid_argument);
    }
    return accepted_channel{std::move(channel), *peer};
}

} // namespace idfxx::net::netconn
