// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/resolver>
 * @file resolver.hpp
 * @brief DNS resolution.
 *
 * @defgroup idfxx_net_resolver Resolver
 * @ingroup idfxx_net
 * @brief Resolve a host name and service to one or more endpoints.
 *
 * @note By default a single address is returned per query. Enable
 *       `CONFIG_LWIP_USE_ESP_GETADDRINFO` to receive multiple records
 *       (subject to `DNS_MAX_HOST_IP`).
 * @{
 */

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <optional>
#include <string_view>
#include <vector>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/resolver>
 * @brief Resolver hints used by `try_resolve` / `resolve`.
 */
struct resolver_options {
    /** @brief Address family preference. `std::nullopt` accepts either family. */
    std::optional<enum family> family = std::nullopt;

    /** @brief Require the host to be a numeric address — skip the DNS lookup. */
    bool numeric_host = false;

    /** @brief Require the service to be a numeric port — skip the service-name lookup. */
    bool numeric_port = false;

    /** @brief Request results suitable for `bind()` rather than `connect()`. */
    bool passive = false;

    /** @brief Transport protocol filter. */
    enum class proto : uint8_t {
        tcp = 6,  ///< TCP only.
        udp = 17, ///< UDP only.
    };

    /** @brief Restrict results to a particular transport protocol. `std::nullopt` accepts any protocol. */
    std::optional<enum proto> proto = std::nullopt;
};

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Resolves a host and numeric port to a list of endpoints.
 *
 * @param host Host name or numeric address.
 * @param port Port number in host byte order.
 * @param opts Resolver options.
 *
 * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
 * @throws std::system_error on failure.
 *
 * @return List of resolved endpoints (may be empty).
 */
[[nodiscard]] std::vector<endpoint> resolve(std::string_view host, port_number port, const resolver_options& opts = {});
#endif

/**
 * @brief Resolves a host and numeric port to a list of endpoints.
 *
 * @param host Host name or numeric address.
 * @param port Port number in host byte order.
 * @param opts Resolver options.
 * @return List of resolved endpoints (may be empty), or an error.
 */
[[nodiscard]] result<std::vector<endpoint>>
try_resolve(std::string_view host, port_number port, const resolver_options& opts = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Resolves a host and service name to a list of endpoints.
 *
 * @param host    Host name or numeric address.
 * @param service Service name (e.g. `"http"`) or numeric port string.
 * @param opts    Resolver options.
 *
 * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
 * @throws std::system_error on failure.
 *
 * @return List of resolved endpoints.
 */
[[nodiscard]] std::vector<endpoint>
resolve(std::string_view host, std::string_view service, const resolver_options& opts = {});
#endif

/**
 * @brief Resolves a host and service name to a list of endpoints.
 *
 * @param host    Host name or numeric address.
 * @param service Service name (e.g. `"http"`) or numeric port string.
 * @param opts    Resolver options.
 * @return List of resolved endpoints, or an error.
 */
[[nodiscard]] result<std::vector<endpoint>>
try_resolve(std::string_view host, std::string_view service, const resolver_options& opts = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Resolves a host and port and returns the first matching endpoint.
 *
 * @param host Host name or numeric address.
 * @param port Port number.
 * @param opts Resolver options.
 *
 * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
 * @throws std::system_error if no endpoint is found or on other failure.
 *
 * @return The first endpoint.
 */
[[nodiscard]] endpoint resolve_one(std::string_view host, port_number port, const resolver_options& opts = {});
#endif

/**
 * @brief Resolves a host and port and returns the first matching endpoint.
 *
 * @param host Host name or numeric address.
 * @param port Port number.
 * @param opts Resolver options.
 * @return The first endpoint, or an error such as `errc::name_not_found`.
 */
[[nodiscard]] result<endpoint>
try_resolve_one(std::string_view host, port_number port, const resolver_options& opts = {});

} // namespace idfxx::net

/** @} */ // end of idfxx_net_resolver
