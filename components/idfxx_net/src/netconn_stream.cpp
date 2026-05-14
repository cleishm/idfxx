// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/stream_connection>

#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/opt.h>
#include <utility>

// Verify the protocol tag for TCP and TCP+IPv6 matches lwIP.
static_assert(NETCONN_TCP == 0x10);
#ifdef CONFIG_LWIP_IPV6
static_assert((NETCONN_TCP | NETCONN_TYPE_IPV6) == NETCONN_TCP_IPV6);
#endif

// Verify write_flag values match lwIP NETCONN_* write-flag constants.
static_assert(std::to_underlying(idfxx::net::netconn::write_flag::copy) == NETCONN_COPY);
static_assert(std::to_underlying(idfxx::net::netconn::write_flag::more) == NETCONN_MORE);
static_assert(std::to_underlying(idfxx::net::netconn::write_flag::dont_block) == NETCONN_DONTBLOCK);

namespace idfxx::net::netconn {

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
stream_connection::stream_connection()
    : stream_connection(idfxx::unwrap(make())) {}

stream_connection::stream_connection(enum family fam)
    : stream_connection(idfxx::unwrap(make(fam))) {}
#endif

result<stream_connection> stream_connection::make() {
    return make(family::ipv4);
}

result<stream_connection> stream_connection::make(enum family fam) {
    auto conn = detail::make_netconn(NETCONN_TCP, fam);
    if (!conn) {
        return error(conn.error());
    }
    return stream_connection(*conn, fam);
}

result<void> stream_connection::try_connect(const endpoint& peer) {
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

result<size_t> stream_connection::try_write(std::span<const std::byte> data, idfxx::flags<write_flag> flags) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    size_t written = 0;
    auto rc = ::netconn_write_partly(
        _conn, data.data(), data.size(), static_cast<uint8_t>(idfxx::to_underlying(flags)), &written
    );
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return written;
}

result<netbuf> stream_connection::try_recv() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    if (_eof) {
        return netbuf{};
    }
    ::netbuf* buf = nullptr;
    auto rc = ::netconn_recv(_conn, &buf);
    if (rc == ERR_CLSD) {
        // Graceful peer close: return a non-owning netbuf so callers can detect
        // EOF via `is_open() == false` rather than catching an exception.
        _eof = true;
        return netbuf{};
    }
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return netbuf(buf);
}

result<std::span<std::byte>> stream_connection::try_recv(std::span<std::byte> buf) {
    auto nb = try_recv();
    if (!nb) {
        return error(nb.error());
    }
    return buf.first(nb->copy_into(buf));
}

result<void> stream_connection::try_close() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_close(_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> stream_connection::try_shutdown() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    auto rc = ::netconn_shutdown(_conn, 1, 1);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> stream_connection::try_shutdown(direction dir) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    const uint8_t shut_rx = (dir == direction::receive) ? 1 : 0;
    const uint8_t shut_tx = (dir == direction::send) ? 1 : 0;
    auto rc = ::netconn_shutdown(_conn, shut_rx, shut_tx);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

} // namespace idfxx::net::netconn
