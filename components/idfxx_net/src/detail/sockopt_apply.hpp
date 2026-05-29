// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal-only header. Shared "apply config" helper that walks the option
// fields common to every socket type (`stream_socket`, `datagram_socket`,
// `raw_socket`, `listener`) so each per-type `apply_config` only handles the
// type-specific extras.

#include "sdkconfig.h"

#include "sockopt.hpp"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <optional>
#include <string_view>

namespace idfxx::net::detail {

/// @brief Subset of fields shared by every socket-type config struct.
///        Each socket's `apply_config` copies into this and forwards to
///        `apply_common_config`.
struct common_config {
    address_family family = address_family::ipv4;
    bool non_blocking = false;
    std::optional<std::chrono::milliseconds> recv_timeout = std::nullopt;
    std::optional<std::chrono::milliseconds> send_timeout = std::nullopt;
    bool reuse_address = false;
    std::optional<std::string_view> bind_to_device = std::nullopt;
#ifdef CONFIG_LWIP_IPV6
    bool ipv6_only = false;
#endif
};

/// @brief Applies the options shared by every socket type. Type-specific
///        options (TCP keep-alive, UDP broadcast, listener backlog, etc.) are
///        the caller's responsibility.
[[nodiscard]] inline result<void> apply_common_config(int fd, const common_config& cfg) noexcept {
    if (cfg.non_blocking) {
        if (auto r = set_non_blocking(fd, true); !r) {
            return r;
        }
    }
    if (cfg.recv_timeout) {
        if (auto r = set_timeout(fd, SO_RCVTIMEO, *cfg.recv_timeout); !r) {
            return r;
        }
    }
    if (cfg.send_timeout) {
        if (auto r = set_timeout(fd, SO_SNDTIMEO, *cfg.send_timeout); !r) {
            return r;
        }
    }
    if (cfg.reuse_address) {
        if (auto r = set_int_option(fd, SOL_SOCKET, SO_REUSEADDR, 1); !r) {
            return r;
        }
    }
#ifdef CONFIG_LWIP_IPV6
    if (cfg.ipv6_only && cfg.family == address_family::ipv6) {
        if (auto r = set_ipv6_only(fd, true); !r) {
            return r;
        }
    }
#endif
    if (cfg.bind_to_device) {
        if (auto r = set_bind_to_device(fd, *cfg.bind_to_device); !r) {
            return r;
        }
    }
    return {};
}

} // namespace idfxx::net::detail
