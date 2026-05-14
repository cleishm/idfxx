// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/netbuf>

#include <algorithm>
#include <lwip/api.h>
#include <lwip/netbuf.h>
#include <utility>

namespace idfxx::net::netconn {

result<netbuf> netbuf::make() {
    ::netbuf* buf = ::netbuf_new();
    if (buf == nullptr) {
        raise_no_mem();
    }
    return netbuf(buf);
}

netbuf::~netbuf() {
    if (_buf != nullptr) {
        ::netbuf_delete(_buf);
        _buf = nullptr;
    }
}

netbuf::netbuf(netbuf&& other) noexcept
    : _buf(std::exchange(other._buf, nullptr)) {}

netbuf& netbuf::operator=(netbuf&& other) noexcept {
    if (this != &other) {
        if (_buf != nullptr) {
            ::netbuf_delete(_buf);
        }
        _buf = std::exchange(other._buf, nullptr);
    }
    return *this;
}

result<std::span<const std::byte>> netbuf::try_data() const {
    if (_buf == nullptr) {
        return error(errc::invalid_state);
    }
    void* ptr = nullptr;
    uint16_t len = 0;
    auto rc = ::netbuf_data(_buf, &ptr, &len);
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return std::span<const std::byte>(static_cast<const std::byte*>(ptr), len);
}

size_t netbuf::len() const noexcept {
    if (_buf == nullptr) {
        return 0;
    }
    return netbuf_len(_buf);
}

bool netbuf::advance() noexcept {
    if (_buf == nullptr) {
        return false;
    }
    return ::netbuf_next(_buf) >= 0;
}

void netbuf::rewind() noexcept {
    if (_buf == nullptr) {
        return;
    }
    ::netbuf_first(_buf);
}

std::optional<endpoint> netbuf::from_endpoint() const noexcept {
    if (_buf == nullptr) {
        return std::nullopt;
    }
    return net::detail::ip_addr_to_endpoint(netbuf_fromaddr(_buf), netbuf_fromport(_buf));
}

size_t netbuf::copy_into(std::span<std::byte> dst) const noexcept {
    if (_buf == nullptr) {
        return 0;
    }
    auto total = netbuf_len(_buf);
    auto to_copy = static_cast<uint16_t>(std::min<size_t>(dst.size(), total));
    return static_cast<size_t>(netbuf_copy_partial(_buf, dst.data(), to_copy, 0));
}

result<void> netbuf::try_attach(std::span<const std::byte> data) {
    if (_buf == nullptr) {
        return error(errc::invalid_state);
    }
    if (data.size() > UINT16_MAX) {
        return error(errc::message_too_long);
    }
    auto rc = ::netbuf_ref(_buf, data.data(), static_cast<uint16_t>(data.size()));
    if (rc != ERR_OK) {
        return error(lwip_err_to_error_code(rc));
    }
    return {};
}

} // namespace idfxx::net::netconn
