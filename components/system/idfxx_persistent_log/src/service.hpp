// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// What the service gives the tests beyond its public API, so that they can see what flash holds
// without changing it.

#include <idfxx/error>

#include <cstddef>
#include <span>

namespace idfxx::persistent_log::internal {

/// Copies the newest complete entries in flash into `out`, as copy_newest() does, but doesn't
/// flush first. So the lines still waiting in RTC memory are not copied.
///
/// @return The number of bytes copied to the front of `out`, or an error.
/// @retval idfxx::errc::invalid_state The log isn't started.
/// @retval other The flash error, if a read fails.
[[nodiscard]] idfxx::result<size_t> copy_newest_in_flash(std::span<char> out);

} // namespace idfxx::persistent_log::internal
