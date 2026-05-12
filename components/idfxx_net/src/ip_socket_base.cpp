// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/select.hpp"
#include "detail/sockaddr.hpp"
#include "detail/sockopt.hpp"

#include <idfxx/net/detail/ip_socket_base.hpp>

#include <algorithm>
#include <errno.h>
#include <esp_log.h>
#include <limits>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <tuple>
#include <utility>

namespace idfxx::net::detail {

namespace {

constexpr const char* TAG = "idfxx_net";

}

ip_socket_base::~ip_socket_base() noexcept {
    close();
}

ip_socket_base::ip_socket_base(ip_socket_base&& other) noexcept
    : _fd(std::exchange(other._fd, -1))
    , _family(other._family) {}

ip_socket_base& ip_socket_base::operator=(ip_socket_base&& other) noexcept {
    if (this != &other) {
        close();
        _fd = std::exchange(other._fd, -1);
        _family = other._family;
    }
    return *this;
}

void ip_socket_base::close() noexcept {
    if (_fd >= 0) {
        if (lwip_close(_fd) != 0) {
            ESP_LOGW(TAG, "close(fd=%d) failed: errno=%d", _fd, errno);
        }
        _fd = -1;
    }
}

endpoint ip_socket_base::local_endpoint() const noexcept {
    if (_fd < 0) {
        return {};
    }
    return get_local_endpoint(_fd).value_or(endpoint{});
}

result<endpoint> ip_socket_base::try_peer_endpoint() const {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    return get_peer_endpoint(_fd);
}

result<void> ip_socket_base::try_bind(const endpoint& addr) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    auto len = to_sockaddr(addr, ss);
    if (len == 0) {
        return error(errc::invalid_argument);
    }
    if (lwip_bind(_fd, reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<void> ip_socket_base::try_connect(const endpoint& peer) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    sockaddr_storage ss{};
    auto len = to_sockaddr(peer, ss);
    if (len == 0) {
        return error(errc::invalid_argument);
    }
    if (lwip_connect(_fd, reinterpret_cast<const sockaddr*>(&ss), len) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<std::span<std::byte>> ip_socket_base::try_recv(std::span<std::byte> buf) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    ssize_t n = lwip_recv(_fd, buf.data(), buf.size(), 0);
    if (n < 0) {
        return error(errno_to_error_code(errno));
    }
    return buf.first(static_cast<size_t>(n));
}

void ip_socket_base::set_non_blocking(bool on) noexcept {
    std::ignore = ::idfxx::net::detail::set_non_blocking(_fd, on);
}

void ip_socket_base::_set_recv_timeout(std::chrono::milliseconds t) noexcept {
    std::ignore = set_timeout(_fd, SO_RCVTIMEO, t);
}

void ip_socket_base::_set_send_timeout(std::chrono::milliseconds t) noexcept {
    std::ignore = set_timeout(_fd, SO_SNDTIMEO, t);
}

void ip_socket_base::set_reuse_address(bool on) noexcept {
    std::ignore = set_int_option(_fd, SOL_SOCKET, SO_REUSEADDR, on ? 1 : 0);
}

void ip_socket_base::set_recv_buffer(size_t bytes) noexcept {
    auto clamped = std::min<size_t>(bytes, std::numeric_limits<int>::max());
    std::ignore = set_int_option(_fd, SOL_SOCKET, SO_RCVBUF, static_cast<int>(clamped));
}

void ip_socket_base::set_send_buffer(size_t bytes) noexcept {
    auto clamped = std::min<size_t>(bytes, std::numeric_limits<int>::max());
    std::ignore = set_int_option(_fd, SOL_SOCKET, SO_SNDBUF, static_cast<int>(clamped));
}

void ip_socket_base::set_ttl(uint8_t ttl) noexcept {
    std::ignore = set_int_option(_fd, IPPROTO_IP, IP_TTL, ttl);
}

void ip_socket_base::set_tos(uint8_t tos) noexcept {
    std::ignore = set_int_option(_fd, IPPROTO_IP, IP_TOS, tos);
}

void ip_socket_base::set_dscp(dscp value) noexcept {
    set_tos(static_cast<uint8_t>((static_cast<uint8_t>(value) & 0x3F) << 2));
}

#ifdef CONFIG_LWIP_IPV6
void ip_socket_base::set_ipv6_only(bool on) noexcept {
    std::ignore = ::idfxx::net::detail::set_ipv6_only(_fd, on);
}
#endif

result<void> ip_socket_base::try_set_bind_to_device(std::string_view ifname) {
    return ::idfxx::net::detail::set_bind_to_device(_fd, ifname);
}

result<void> ip_socket_base::_try_wait_readable(std::chrono::milliseconds timeout) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    return wait(_fd, wait_for::read, timeout);
}

result<void> ip_socket_base::_try_wait_writable(std::chrono::milliseconds timeout) {
    if (_fd < 0) {
        return error(errc::invalid_state);
    }
    return wait(_fd, wait_for::write, timeout);
}

size_t ip_socket_base::get_recv_buffer() const noexcept {
    if (_fd < 0) {
        return 0;
    }
    auto r = get_int_option(_fd, SOL_SOCKET, SO_RCVBUF);
    return r ? static_cast<size_t>(*r) : 0;
}

size_t ip_socket_base::get_send_buffer() const noexcept {
    if (_fd < 0) {
        return 0;
    }
    auto r = get_int_option(_fd, SOL_SOCKET, SO_SNDBUF);
    return r ? static_cast<size_t>(*r) : 0;
}

int ip_socket_base::get_last_error() const noexcept {
    if (_fd < 0) {
        return 0;
    }
    auto r = get_int_option(_fd, SOL_SOCKET, SO_ERROR);
    return r.value_or(0);
}

} // namespace idfxx::net::detail
