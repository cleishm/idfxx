// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/http/types>
 * @file types.hpp
 * @brief Common HTTP types.
 *
 * @defgroup idfxx_http HTTP Types Component
 * @brief Common HTTP type definitions for ESP32.
 *
 * Provides type-safe enums for HTTP methods, authentication types, and
 * transport types, along with string conversion and formatting support.
 *
 * Depends on @ref idfxx_core for error handling.
 * @{
 */

#include <string>

namespace idfxx::http {

/**
 * @headerfile <idfxx/http/types>
 * @brief HTTP request methods.
 */
enum class method : int {
    // clang-format off
    get         = 0,  ///< GET
    post        = 1,  ///< POST
    put         = 2,  ///< PUT
    patch       = 3,  ///< PATCH
    delete_     = 4,  ///< DELETE (trailing underscore — C++ keyword)
    head        = 5,  ///< HEAD
    notify      = 6,  ///< NOTIFY
    subscribe   = 7,  ///< SUBSCRIBE
    unsubscribe = 8,  ///< UNSUBSCRIBE
    options     = 9,  ///< OPTIONS
    copy        = 10, ///< COPY
    move        = 11, ///< MOVE
    lock        = 12, ///< LOCK
    unlock      = 13, ///< UNLOCK
    propfind    = 14, ///< PROPFIND
    proppatch   = 15, ///< PROPPATCH
    mkcol       = 16, ///< MKCOL
    report      = 17, ///< REPORT
    // clang-format on
};

/**
 * @headerfile <idfxx/http/types>
 * @brief HTTP authentication types.
 */
enum class auth_type : int {
    // clang-format off
    none   = 0, ///< No authentication
    basic  = 1, ///< HTTP Basic authentication
    digest = 2, ///< HTTP Digest authentication
    // clang-format on
};

/**
 * @headerfile <idfxx/http/types>
 * @brief HTTP transport types.
 */
enum class transport : int {
    // clang-format off
    unknown = 0, ///< Unknown transport
    tcp     = 1, ///< Plain TCP (HTTP)
    ssl     = 2, ///< SSL/TLS (HTTPS)
    // clang-format on
};

} // namespace idfxx::http

namespace idfxx {

/**
 * @headerfile <idfxx/http/types>
 * @brief Returns a string representation of an HTTP method.
 *
 * @param m The HTTP method to convert.
 * @return "GET", "POST", etc., or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(http::method m);

/**
 * @headerfile <idfxx/http/types>
 * @brief Returns a string representation of an HTTP authentication type.
 *
 * @param t The authentication type to convert.
 * @return "NONE", "BASIC", "DIGEST", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(http::auth_type t);

/**
 * @headerfile <idfxx/http/types>
 * @brief Returns a string representation of an HTTP transport type.
 *
 * @param t The transport type to convert.
 * @return "UNKNOWN", "TCP", "SSL", or "unknown(N)" for unrecognized values.
 */
[[nodiscard]] std::string to_string(http::transport t);

} // namespace idfxx

#include "sdkconfig.h"
#ifdef CONFIG_IDFXX_STD_FORMAT
/** @cond INTERNAL */
#include <algorithm>
#include <format>
namespace std {

template<>
struct formatter<idfxx::http::method> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::http::method m, FormatContext& ctx) const {
        auto s = idfxx::to_string(m);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::http::auth_type> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::http::auth_type t, FormatContext& ctx) const {
        auto s = idfxx::to_string(t);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

template<>
struct formatter<idfxx::http::transport> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(idfxx::http::transport t, FormatContext& ctx) const {
        auto s = idfxx::to_string(t);
        return std::copy(s.begin(), s.end(), ctx.out());
    }
};

} // namespace std
/** @endcond */
#endif // CONFIG_IDFXX_STD_FORMAT

/** @} */ // end of idfxx_http
