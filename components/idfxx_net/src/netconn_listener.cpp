// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include <idfxx/net/netconn/listener>

#include <lwip/api.h>
#include <lwip/opt.h>
#include <utility>

namespace idfxx::net::netconn {

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
listener::listener()
    : listener(idfxx::unwrap(make())) {}

listener::listener(enum family fam)
    : listener(idfxx::unwrap(make(fam))) {}
#endif

result<listener> listener::make() {
    return make(family::ipv4);
}

result<listener> listener::make(enum family fam) {
    auto conn = detail::make_netconn(NETCONN_TCP, fam);
    if (!conn) {
        return error(conn.error());
    }
    return listener(*conn, fam);
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

result<stream_connection> listener::try_accept() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    ::netconn* new_conn = nullptr;
    auto rc = ::netconn_accept(_conn, &new_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return stream_connection(new_conn, _family);
}

} // namespace idfxx::net::netconn
