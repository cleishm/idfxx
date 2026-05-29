// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/stream_channel>

#include <algorithm>
#include <lwip/api.h>
#include <lwip/ip_addr.h>
#include <lwip/netbuf.h>
#include <lwip/opt.h>
#include <utility>

// Verify the protocol tag for TCP and TCP+IPv6 matches lwIP.
static_assert(NETCONN_TCP == 0x10);
#ifdef CONFIG_LWIP_IPV6
static_assert((NETCONN_TCP | NETCONN_TYPE_IPV6) == NETCONN_TCP_IPV6);
#endif

// The `more` / `dont_block` flags pass straight through to lwIP, so their
// values must match. `no_copy` is translated explicitly in try_send (copy is
// the default), so it is not required to match a NETCONN_* bit.
static_assert(std::to_underlying(idfxx::net::netconn::write_flag::more) == NETCONN_MORE);
static_assert(std::to_underlying(idfxx::net::netconn::write_flag::dont_block) == NETCONN_DONTBLOCK);

namespace idfxx::net::netconn {

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
stream_channel::stream_channel()
    : stream_channel(idfxx::unwrap(make())) {}

stream_channel::stream_channel(address_family fam)
    : stream_channel(idfxx::unwrap(make(fam))) {}
#endif

result<stream_channel> stream_channel::make() {
    return make(address_family::ipv4);
}

result<stream_channel> stream_channel::make(address_family fam) {
    auto conn = detail::make_netconn(NETCONN_TCP, fam);
    if (!conn) {
        return error(conn.error());
    }
    return stream_channel(*conn, fam);
}

result<void> stream_channel::try_connect(const endpoint& peer) {
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

result<size_t> stream_channel::try_send(std::span<const std::byte> data, idfxx::flags<write_flag> flags) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    // Copy by default; reference the caller's buffer only when no_copy is set.
    uint8_t apiflags = flags.contains(write_flag::no_copy) ? NETCONN_NOCOPY : NETCONN_COPY;
    if (flags.contains(write_flag::more)) {
        apiflags |= NETCONN_MORE;
    }
    if (flags.contains(write_flag::dont_block)) {
        apiflags |= NETCONN_DONTBLOCK;
    }
    size_t written = 0;
    auto rc = ::netconn_write_partly(_conn, data.data(), data.size(), apiflags, &written);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return written;
}

result<size_t> stream_channel::try_send(std::string_view data, idfxx::flags<write_flag> flags) {
    return try_send(std::as_bytes(std::span<const char>(data.data(), data.size())), flags);
}

result<buffer> stream_channel::try_recv() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    if (_eof) {
        return buffer{};
    }
    ::netbuf* buf = nullptr;
    auto rc = ::netconn_recv(_conn, &buf);
    if (rc == ERR_CLSD) {
        // Graceful peer close: return a non-owning buffer so callers can detect
        // EOF via `is_open() == false` rather than catching an exception.
        _eof = true;
        return buffer{};
    }
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return buffer(buf);
}

result<std::span<std::byte>> stream_channel::try_recv(std::span<std::byte> buf) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    if (buf.empty()) {
        return buf;
    }

    // Drain leftover from a previous oversized receive first so the byte
    // stream is preserved across the segment boundary.
    if (_pending.is_open()) {
        const size_t total = _pending.len();
        const size_t remaining = total - _pending_offset;
        const auto to_copy = static_cast<uint16_t>(std::min(remaining, buf.size()));
        const auto copied =
            ::netbuf_copy_partial(_pending.idf_handle(), buf.data(), to_copy, static_cast<uint16_t>(_pending_offset));
        _pending_offset += copied;
        if (_pending_offset >= total) {
            _pending = buffer{};
            _pending_offset = 0;
        }
        return buf.first(copied);
    }

    // Fetch the next netbuf from the stack.
    auto nb = try_recv();
    if (!nb) {
        return error(nb.error());
    }
    const size_t nb_len = nb->len();
    if (nb_len <= buf.size()) {
        return buf.first(nb->copy_into(buf));
    }

    // Caller buffer is smaller than the segment lwIP delivered: copy what fits
    // and stash the netbuf so the leftover can be returned on later calls.
    const auto copied = ::netbuf_copy_partial(nb->idf_handle(), buf.data(), static_cast<uint16_t>(buf.size()), 0);
    _pending = std::move(*nb);
    _pending_offset = copied;
    return buf.first(copied);
}

result<void> stream_channel::try_close() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    _pending = buffer{};
    _pending_offset = 0;
    auto rc = ::netconn_close(_conn);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> stream_channel::try_shutdown() {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    _pending = buffer{};
    _pending_offset = 0;
    auto rc = ::netconn_shutdown(_conn, 1, 1);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

result<void> stream_channel::try_shutdown(direction dir) {
    if (_conn == nullptr) {
        return error(errc::invalid_state);
    }
    if (dir == direction::receive) {
        _pending = buffer{};
        _pending_offset = 0;
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
