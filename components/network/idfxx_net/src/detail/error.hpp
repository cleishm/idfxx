// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

// Internal-only header. Translations from the error values that lwIP's socket, netconn and
// resolver APIs report to `idfxx::net` error codes.

#include <idfxx/net/error>

#include <system_error>

namespace idfxx::net {

// Translates a POSIX errno value (positive) to an `idfxx::net` error code. Maps common errno
// values onto the matching `errc` enumerator and falls back to `errc::io_error`. `ENOMEM` and
// `ENOBUFS` map to `errc::no_buffer_space`, so PBUF/transient memory pressure surfaces as a
// recoverable error rather than a fatal raise.
[[nodiscard]] std::error_code errno_to_error_code(int errno_value) noexcept;

// Translates an lwIP `err_t` value (negative on error) to an `idfxx::net` error code. Maps
// `ERR_*` values onto the matching `errc` enumerator. `ERR_MEM` maps to `errc::no_buffer_space`,
// so PBUF/transient memory pressure surfaces as a recoverable error rather than a fatal raise.
[[nodiscard]] std::error_code lwip_err_to_error_code(int err_t_value) noexcept;

// Translates a `getaddrinfo` `EAI_*` value to an `idfxx::net` error code. `EAI_MEMORY` maps to
// `errc::no_buffer_space`.
[[nodiscard]] std::error_code gai_to_error_code(int gai) noexcept;

} // namespace idfxx::net
