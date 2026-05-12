// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#include "detail/sockopt.hpp"

#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <net/if.h>

namespace idfxx::net::detail {

result<void> set_non_blocking(int fd, bool on) noexcept {
    if (fd < 0) {
        return error(errc::invalid_state);
    }
    int flags = lwip_fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return error(errno_to_error_code(errno));
    }
    int new_flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (lwip_fcntl(fd, F_SETFL, new_flags) < 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<void> set_int_option(int fd, int level, int name, int value) noexcept {
    if (fd < 0) {
        return error(errc::invalid_state);
    }
    if (lwip_setsockopt(fd, level, name, &value, sizeof(value)) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<int> get_int_option(int fd, int level, int name) noexcept {
    if (fd < 0) {
        return error(errc::invalid_state);
    }
    int value = 0;
    socklen_t len = sizeof(value);
    if (lwip_getsockopt(fd, level, name, &value, &len) != 0) {
        return error(errno_to_error_code(errno));
    }
    return value;
}

#ifdef CONFIG_LWIP_IPV6
result<void> set_ipv6_only(int fd, bool on) noexcept {
    return set_int_option(fd, IPPROTO_IPV6, IPV6_V6ONLY, on ? 1 : 0);
}
#endif

result<void> set_bind_to_device(int fd, std::string_view ifname) noexcept {
    if (fd < 0) {
        return error(errc::invalid_state);
    }
    if (ifname.size() >= IFNAMSIZ) {
        return error(errc::invalid_argument);
    }
    char buf[IFNAMSIZ];
    std::memcpy(buf, ifname.data(), ifname.size());
    buf[ifname.size()] = '\0';
    if (lwip_setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, buf, ifname.size() + 1) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

result<void> set_timeout(int fd, int optname, std::chrono::milliseconds t) noexcept {
    if (fd < 0) {
        return error(errc::invalid_state);
    }
    timeval tv;
    tv.tv_sec = static_cast<long>(t.count() / 1000);
    tv.tv_usec = static_cast<long>((t.count() % 1000) * 1000);
    if (lwip_setsockopt(fd, SOL_SOCKET, optname, &tv, sizeof(tv)) != 0) {
        return error(errno_to_error_code(errno));
    }
    return {};
}

} // namespace idfxx::net::detail
