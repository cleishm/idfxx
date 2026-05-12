// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/error>
 * @file error.hpp
 * @brief Error codes for the IP transport layer.
 *
 * @defgroup idfxx_net IP Transport
 * @brief Type-safe IP transport: TCP, UDP, raw IP, DNS, and a lower-level Netconn API.
 *
 * Offers protocol-specific socket types (`stream_socket` for TCP,
 * `datagram_socket` for UDP, `raw_socket` for raw IP), a `listener` for
 * accepting TCP connections, free functions for DNS resolution, and the
 * `netconn` sub-namespace for zero-copy receive paths. All APIs are
 * RAII-managed and offer both `result<T>` and exception-based forms.
 *
 * Depends on @ref idfxx_core for error handling and IP address types.
 *
 * @defgroup idfxx_net_error Error Handling
 * @ingroup idfxx_net
 * @brief Error codes and category for `idfxx_net`.
 *
 * The category's `default_error_condition()` member maps each `errc` value to
 * its standard `std::errc` synonym (`operation_would_block`, `timed_out`,
 * `connection_refused`, …), so callers can compare error codes against either
 * the domain-specific `errc` values or the portable POSIX names. The mapping is
 * symmetric, so it also canonicalizes near-duplicate codes — the socket-path
 * `connection_reset` and the netconn-path `netconn_reset` share one condition.
 * @{
 */

#include <idfxx/error>

#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/error>
 * @brief IP transport error codes.
 *
 * The category maps each code to its corresponding `std::errc` synonym (via
 * `default_error_condition`), so for example
 * @code
 * ec == idfxx::net::errc::operation_would_block
 * ec == std::errc::operation_would_block
 * @endcode
 * are both true for the same underlying error.
 */
enum class errc : esp_err_t {
    // clang-format off
    // ----- sockets -----
    invalid_state         = 1,  /*!< Operation invalid in current state. */
    invalid_argument      = 2,  /*!< Invalid argument. */
    not_supported         = 3,  /*!< Operation not supported. */
    timed_out             = 4,  /*!< Operation timed out. */
    operation_would_block = 5,  /*!< Operation would block. */
    in_progress           = 6,  /*!< Operation in progress. */
    interrupted           = 7,  /*!< Operation interrupted by signal. */
    address_in_use        = 8,  /*!< Address already in use. */
    address_not_available = 9,  /*!< Address not available on this host. */
    connection_refused    = 10, /*!< Connection refused by peer. */
    connection_reset      = 11, /*!< Connection reset by peer. */
    connection_aborted    = 12, /*!< Connection aborted. */
    not_connected         = 13, /*!< Socket not connected. */
    already_connected     = 14, /*!< Socket already connected. */
    host_unreachable      = 15, /*!< Host unreachable. */
    network_unreachable   = 16, /*!< Network unreachable. */
    network_down          = 17, /*!< Network is down. */
    broken_pipe           = 18, /*!< Broken pipe (write to closed peer). */
    message_too_long      = 19, /*!< Message too long for transport. */
    too_many_files_open   = 20, /*!< Too many open files / sockets. */
    name_resolution       = 21, /*!< Generic name resolution failure. */
    name_not_found        = 22, /*!< Host name not found (NXDOMAIN). */
    name_temporary        = 23, /*!< Temporary name resolution failure. */
    name_no_data          = 24, /*!< Name has no data of requested family. */
    io_error              = 25, /*!< Generic I/O error. */
    wrong_protocol_type   = 26, /*!< Operation does not match socket's family or protocol. */
    unexpected_eof        = 27, /*!< Stream ended before the requested data was received. */

    // ----- netconn -----
    netconn_buffer_error  = 28, /*!< Netconn buffer error. */
    netconn_routing       = 29, /*!< Netconn routing error. */
    netconn_illegal_value = 30, /*!< Illegal value passed to netconn. */
    netconn_aborted       = 31, /*!< Netconn connection aborted. */
    netconn_reset         = 32, /*!< Netconn connection reset. */
    netconn_closed        = 33, /*!< Netconn connection closed. */

    no_buffer_space       = 34, /*!< Out of buffer/PBUF memory on a data-path call (transient). */
    // clang-format on
};

/**
 * @headerfile <idfxx/net/error>
 * @brief Error category for `idfxx_net` errors.
 *
 * Returns human-readable names for `errc` values and maps each code to its
 * standard `std::errc` synonym via `default_error_condition()`.
 */
class error_category : public std::error_category {
public:
    /** @brief Returns the name of the error category. */
    [[nodiscard]] const char* name() const noexcept override final;

    /** @brief Returns a human-readable message for the given error code. */
    [[nodiscard]] std::string message(int ec) const override final;

    /**
     * @brief Maps an `errc` value to its canonical portable condition.
     *
     * Returns the equivalent `std::errc` condition for codes that have a POSIX
     * synonym (e.g. `operation_would_block`, `connection_reset`); codes without
     * one (DNS failures, `netconn_closed`, …) map to a condition in this
     * category. Because the runtime drives `==`/`!=` comparisons between an
     * `std::error_code` and an `std::error_condition` through this mapping,
     * callers may compare against either form:
     * @code
     * ec == idfxx::net::errc::operation_would_block
     * ec == std::errc::operation_would_block
     * @endcode
     * The mapping is symmetric, so two codes that share a synonym
     * (`connection_reset` and `netconn_reset`) compare equal to the same
     * `std::errc` and to each other's condition.
     *
     * @param code The `errc` value to canonicalize.
     * @return The canonical `std::error_condition` for `code`.
     */
    [[nodiscard]] std::error_condition default_error_condition(int code) const noexcept override final;
};

} // namespace idfxx::net

namespace idfxx {

/**
 * @headerfile <idfxx/net/error>
 * @brief Returns a reference to the `idfxx_net` error category singleton.
 *
 * @return Reference to the singleton `net::error_category` instance.
 */
[[nodiscard]] const net::error_category& net_category() noexcept;

} // namespace idfxx

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/error>
 * @brief Creates an error code from an `idfxx::net::errc` value.
 *
 * @param e The error code enumerator.
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] inline std::error_code make_error_code(errc e) noexcept {
    return {std::to_underlying(e), net_category()};
}

/** @cond INTERNAL */

/**
 * @brief Translates a POSIX errno value to an `idfxx::net` error code.
 *
 * Used internally by the socket implementation. Maps common errno values
 * onto the matching `errc` enumerator; falls back to `errc::io_error`.
 * `ENOMEM` and `ENOBUFS` map to `errc::no_buffer_space` so PBUF/transient
 * memory pressure surfaces as a recoverable error rather than a fatal raise.
 *
 * @param errno_value The errno value (positive integer).
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] std::error_code errno_to_error_code(int errno_value) noexcept;

/**
 * @brief Translates an lwIP `err_t` value to an `idfxx::net` error code.
 *
 * Used internally by the netconn implementation. Maps `ERR_*` values onto
 * the matching `errc` enumerator. `ERR_MEM` maps to `errc::no_buffer_space`
 * so PBUF/transient memory pressure surfaces as a recoverable error rather
 * than a fatal raise.
 *
 * @param err_t_value The lwIP `err_t` value (negative on error).
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] std::error_code lwip_err_to_error_code(int err_t_value) noexcept;

/**
 * @brief Translates a `getaddrinfo` `EAI_*` value to an `idfxx::net` error code.
 *
 * Used internally by the resolver implementation. `EAI_MEMORY` maps to
 * `errc::no_buffer_space`.
 *
 * @param gai The `EAI_*` value.
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] std::error_code gai_to_error_code(int gai) noexcept;

/** @endcond */

} // namespace idfxx::net

/// @cond INTERNAL
namespace std {
template<>
struct is_error_code_enum<idfxx::net::errc> : std::true_type {};
} // namespace std
/// @endcond

/** @} */ // end of idfxx_net_error

/**
 * @defgroup idfxx_net_netconn Netconn
 * @ingroup idfxx_net
 * @brief Lower-level networking API with zero-copy receive.
 *
 * The Netconn API exposes received buffers directly to the caller, enabling
 * zero-copy receive paths and tighter control over send/receive behaviour at
 * the cost of a more involved programming model.
 *
 * @note Most callers should prefer the BSD socket API
 *       (@ref idfxx_net_stream_socket and friends); the Netconn types are
 *       provided for cases where zero-copy receive or fine-grained
 *       send-flag control are needed.
 */
