// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal-only header. Shared socket-option and errno-translation helpers used
// by the socket types and `listener` to avoid duplicating the same setsockopt +
// errno-translate code.

#include <idfxx/net/error>

#include "sdkconfig.h"

#include <chrono>
#include <string_view>
#include <system_error>

namespace idfxx::net::detail {

[[nodiscard]] result<void> set_non_blocking(int fd, bool on) noexcept;
[[nodiscard]] result<void> set_int_option(int fd, int level, int name, int value) noexcept;
[[nodiscard]] result<int> get_int_option(int fd, int level, int name) noexcept;
#ifdef CONFIG_LWIP_IPV6
[[nodiscard]] result<void> set_ipv6_only(int fd, bool on) noexcept;
#endif
[[nodiscard]] result<void> set_bind_to_device(int fd, std::string_view ifname) noexcept;
[[nodiscard]] result<void> set_timeout(int fd, int optname, std::chrono::milliseconds t) noexcept;

// Translates the errno from a failed recv / send / accept on @p fd. lwIP reports
// an expired SO_RCVTIMEO / SO_SNDTIMEO as EWOULDBLOCK, indistinguishable by
// errno from a non-blocking socket with nothing to do; on a blocking descriptor
// that can only be the timeout, so it maps to `errc::timed_out`. Everything
// else (including EWOULDBLOCK on a non-blocking descriptor, or a descriptor
// whose flags cannot be read) uses `errno_to_error_code`.
//
// Pass the errno captured immediately after the failing call: this function
// itself calls lwip_fcntl, which overwrites errno.
[[nodiscard]] std::error_code io_error_code(int fd, int errno_value) noexcept;

} // namespace idfxx::net::detail
