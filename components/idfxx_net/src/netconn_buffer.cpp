// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/ip_addr.hpp"

#include <idfxx/net/netconn/buffer>

#include <algorithm>
#include <lwip/api.h>
#include <lwip/netbuf.h>
#include <lwip/pbuf.h>
#include <ranges>
#include <utility>

namespace idfxx::net::netconn {

static_assert(std::forward_iterator<buffer::segment_view::iterator>);
static_assert(std::ranges::forward_range<buffer::segment_view>);

result<buffer> buffer::make() {
    ::netbuf* buf = ::netbuf_new();
    if (buf == nullptr) {
        return error(errc::no_buffer_space);
    }
    return buffer(buf);
}

buffer::~buffer() {
    if (_buf != nullptr) {
        ::netbuf_delete(_buf);
        _buf = nullptr;
    }
}

buffer::buffer(buffer&& other) noexcept
    : _buf(std::exchange(other._buf, nullptr)) {}

buffer& buffer::operator=(buffer&& other) noexcept {
    if (this != &other) {
        if (_buf != nullptr) {
            ::netbuf_delete(_buf);
        }
        _buf = std::exchange(other._buf, nullptr);
    }
    return *this;
}

buffer::segment_view::iterator::value_type buffer::segment_view::iterator::operator*() const noexcept {
    const auto* p = static_cast<const ::pbuf*>(_seg);
    return {static_cast<const std::byte*>(p->payload), p->len};
}

buffer::segment_view::iterator& buffer::segment_view::iterator::operator++() noexcept {
    _seg = static_cast<const ::pbuf*>(_seg)->next;
    return *this;
}

buffer::segment_view::iterator buffer::segment_view::begin() const noexcept {
    return iterator{_buf != nullptr ? _buf->p : nullptr};
}

size_t buffer::len() const noexcept {
    if (_buf == nullptr) {
        return 0;
    }
    return netbuf_len(_buf);
}

std::optional<endpoint> buffer::from_endpoint() const noexcept {
    if (_buf == nullptr) {
        return std::nullopt;
    }
    return net::detail::ip_addr_to_endpoint(netbuf_fromaddr(_buf), netbuf_fromport(_buf));
}

size_t buffer::copy_into(std::span<std::byte> dst) const noexcept {
    if (_buf == nullptr) {
        return 0;
    }
    auto total = netbuf_len(_buf);
    auto to_copy = static_cast<uint16_t>(std::min<size_t>(dst.size(), total));
    return static_cast<size_t>(netbuf_copy_partial(_buf, dst.data(), to_copy, 0));
}

result<void> buffer::try_attach(std::span<const std::byte> data) {
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
