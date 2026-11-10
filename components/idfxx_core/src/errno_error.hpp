// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Private to idfxx_core; not part of the public API.

#include <idfxx/error>

#include <cerrno>
#include <system_error>

namespace idfxx::detail {

/**
 * @brief Builds an error from the current `errno`.
 *
 * Callers clear `errno` before the failing call, so a stale value is not
 * reported. Returns `errc::fail` if the call set no `errno`.
 */
[[nodiscard]] inline std::unexpected<std::error_code> errno_error() noexcept {
    const int err = errno;
    if (err == 0) {
        return error(errc::fail);
    }
    return error(std::error_code(err, std::generic_category()));
}

} // namespace idfxx::detail
