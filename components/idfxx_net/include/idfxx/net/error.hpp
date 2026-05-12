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
 * The category's `equivalent()` member declares which `errc` values are
 * equivalent to the standard `std::errc` synonyms (`operation_would_block`,
 * `timed_out`, `connection_refused`, …), so callers can compare error codes
 * against either the domain-specific `errc` values or the portable POSIX names.
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
 * The category's `equivalent()` member recognises the codes as equivalent to
 * the corresponding `std::errc` synonyms, so for example
 * @code
 * ec == idfxx::net::errc::would_block
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
    timed_out             = 5,  /*!< Operation timed out. */
    would_block           = 6,  /*!< Operation would block. */
    in_progress           = 7,  /*!< Operation in progress. */
    interrupted           = 8,  /*!< Operation interrupted by signal. */
    address_in_use        = 9,  /*!< Address already in use. */
    address_not_available = 10, /*!< Address not available on this host. */
    connection_refused    = 11, /*!< Connection refused by peer. */
    connection_reset      = 12, /*!< Connection reset by peer. */
    connection_aborted    = 13, /*!< Connection aborted. */
    not_connected         = 14, /*!< Socket not connected. */
    already_connected     = 15, /*!< Socket already connected. */
    host_unreachable      = 16, /*!< Host unreachable. */
    network_unreachable   = 17, /*!< Network unreachable. */
    network_down          = 18, /*!< Network is down. */
    pipe_broken           = 19, /*!< Pipe broken (write to closed peer). */
    message_too_long      = 20, /*!< Message too long for transport. */
    too_many_open         = 21, /*!< Too many open files / sockets. */
    name_resolution       = 22, /*!< Generic name resolution failure. */
    name_not_found        = 23, /*!< Host name not found (NXDOMAIN). */
    name_temporary        = 24, /*!< Temporary name resolution failure. */
    name_no_data          = 25, /*!< Name has no data of requested family. */
    io_error              = 26, /*!< Generic I/O error. */
    wrong_protocol_type   = 27, /*!< Operation does not match socket's family or protocol. */

    // ----- netconn -----
    netconn_buffer_error  = 28, /*!< Netconn buffer error. */
    netconn_routing       = 29, /*!< Netconn routing error. */
    netconn_illegal_value = 30, /*!< Illegal value passed to netconn. */
    netconn_aborted       = 31, /*!< Netconn connection aborted. */
    netconn_reset         = 32, /*!< Netconn connection reset. */
    netconn_closed        = 33, /*!< Netconn connection closed. */
    // clang-format on
};

/**
 * @headerfile <idfxx/net/error>
 * @brief Error category for `idfxx_net` errors.
 *
 * Returns human-readable names for `errc` values and provides
 * `equivalent()` mappings to standard `std::errc` synonyms.
 */
class error_category : public std::error_category {
public:
    /** @brief Returns the name of the error category. */
    [[nodiscard]] const char* name() const noexcept override final;

    /** @brief Returns a human-readable message for the given error code. */
    [[nodiscard]] std::string message(int ec) const override final;

    /**
     * @brief Tests whether an `errc` value is equivalent to a standard condition.
     *
     * Called by the runtime during `==`/`!=` comparisons between an
     * `std::error_code` from this category and an `std::error_condition`
     * (typically a `std::errc`). Returns true when our code corresponds to
     * the given portable condition, allowing callers to write either form:
     * @code
     * ec == idfxx::net::errc::would_block
     * ec == std::errc::operation_would_block
     * @endcode
     *
     * @param code The `errc` value being compared.
     * @param condition The condition being compared against.
     * @return True if `code` is equivalent to `condition`.
     */
    [[nodiscard]] bool equivalent(int code, const std::error_condition& condition) const noexcept override final;
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
 * Raises a fatal out-of-memory error (`raise_no_mem`) on `ENOMEM`.
 *
 * @param errno_value The errno value (positive integer).
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] std::error_code errno_to_error_code(int errno_value);

/**
 * @brief Translates an lwIP `err_t` value to an `idfxx::net` error code.
 *
 * Used internally by the netconn implementation. Maps `ERR_*` values onto
 * the matching `errc` enumerator. Raises a fatal out-of-memory error
 * (`raise_no_mem`) on `ERR_MEM`.
 *
 * @param err_t_value The lwIP `err_t` value (negative on error).
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] std::error_code lwip_err_to_error_code(int err_t_value);

/**
 * @brief Translates a `getaddrinfo` `EAI_*` value to an `idfxx::net` error code.
 *
 * Used internally by the resolver implementation. Raises a fatal
 * out-of-memory error (`raise_no_mem`) on `EAI_MEMORY`.
 *
 * @param gai The `EAI_*` value.
 * @return The corresponding `std::error_code`.
 */
[[nodiscard]] std::error_code gai_to_error_code(int gai);

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
