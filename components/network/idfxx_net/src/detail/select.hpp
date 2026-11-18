// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal helpers for waiting on a socket using lwip_select.

#include <idfxx/error>
#include <idfxx/flags>
#include <idfxx/net/error>

#include <chrono>

namespace idfxx::net::detail {

enum class wait_for : unsigned {
    read = 1u << 0,
    write = 1u << 1,
    except = 1u << 2,
};

} // namespace idfxx::net::detail

/// @cond INTERNAL
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::net::detail::wait_for> = true;
/// @endcond

namespace idfxx::net::detail {

/// Wait for any of the given events on @p fd. Returns:
///   - {} on the event firing
///   - errc::timed_out if the timeout fires
///   - some other error otherwise
[[nodiscard]] idfxx::result<void>
wait(int fd, idfxx::flags<wait_for> what, std::chrono::milliseconds timeout) noexcept;

} // namespace idfxx::net::detail
