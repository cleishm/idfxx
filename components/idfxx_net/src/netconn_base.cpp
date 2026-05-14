// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/detail/base_connection.hpp>

#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/opt.h>
#include <utility>

namespace idfxx::net::netconn::detail {

namespace {

[[nodiscard]] result<int> resolve_netconn_type(int base_type, enum family fam) {
    if (fam == family::ipv6) {
#ifdef CONFIG_LWIP_IPV6
        return base_type | NETCONN_TYPE_IPV6;
#else
        return error(errc::not_supported);
#endif
    }
    return base_type;
}

} // namespace

result<::netconn*> make_netconn(int base_type, enum family fam) {
    auto t = resolve_netconn_type(base_type, fam);
    if (!t) {
        return error(t.error());
    }
    ::netconn* conn = ::netconn_new(static_cast<::netconn_type>(*t));
    if (conn == nullptr) {
        raise_no_mem();
    }
    return conn;
}

result<::netconn*> make_netconn_with_proto(int base_type, uint8_t proto, enum family fam) {
    auto t = resolve_netconn_type(base_type, fam);
    if (!t) {
        return error(t.error());
    }
    ::netconn* conn = ::netconn_new_with_proto_and_callback(static_cast<::netconn_type>(*t), proto, nullptr);
    if (conn == nullptr) {
        raise_no_mem();
    }
    return conn;
}

base_connection::~base_connection() noexcept {
    if (_conn != nullptr) {
        // netconn_delete() skips the pcb teardown when NETCONN_FLAG_MBOXINVALID
        // is set, which lwIP sets itself after a peer FIN half-closes the RX
        // side. Without an explicit prepare step the pcb would still be live
        // when netconn_free() runs, tripping its "pcb must be deallocated"
        // assert. Mirror lwIP's own socket layer (lwip_close) by running the
        // teardown first.
        ::netconn_prepare_delete(_conn);
        ::netconn_delete(_conn);
        _conn = nullptr;
    }
}

base_connection::base_connection(base_connection&& other) noexcept
    : _conn(std::exchange(other._conn, nullptr))
    , _family(other._family) {}

base_connection& base_connection::operator=(base_connection&& other) noexcept {
    if (this != &other) {
        if (_conn != nullptr) {
            ::netconn_delete(_conn);
        }
        _conn = std::exchange(other._conn, nullptr);
        _family = other._family;
    }
    return *this;
}

result<void> base_connection::try_bind(const endpoint& addr) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto bound = net::detail::endpoint_to_ip_addr(addr);
    if (!bound) {
        return error(errc::invalid_argument);
    }
    auto& [ip, port] = *bound;
    auto rc = ::netconn_bind(_conn, &ip, port);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

void base_connection::_set_recv_timeout(std::chrono::milliseconds t) noexcept {
    if (_conn == nullptr) {
        return;
    }
    netconn_set_recvtimeout(_conn, static_cast<int>(t.count()));
}

int base_connection::get_recv_timeout() const noexcept {
    if (_conn == nullptr) {
        return 0;
    }
    return netconn_get_recvtimeout(_conn);
}

void base_connection::set_non_blocking(bool on) noexcept {
    if (_conn == nullptr) {
        return;
    }
    netconn_set_nonblocking(_conn, on ? 1 : 0);
}

} // namespace idfxx::net::netconn::detail
