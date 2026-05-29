// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal-only header. Shared socket-option helpers used by both `socket` and
// `listener` to avoid duplicating the same setsockopt + errno-translate code.

#include <idfxx/net/error>

#include "sdkconfig.h"

#include <chrono>
#include <string_view>

namespace idfxx::net::detail {

[[nodiscard]] result<void> set_non_blocking(int fd, bool on) noexcept;
[[nodiscard]] result<void> set_int_option(int fd, int level, int name, int value) noexcept;
[[nodiscard]] result<int> get_int_option(int fd, int level, int name) noexcept;
#ifdef CONFIG_LWIP_IPV6
[[nodiscard]] result<void> set_ipv6_only(int fd, bool on) noexcept;
#endif
[[nodiscard]] result<void> set_bind_to_device(int fd, std::string_view ifname) noexcept;
[[nodiscard]] result<void> set_timeout(int fd, int optname, std::chrono::milliseconds t) noexcept;

} // namespace idfxx::net::detail
