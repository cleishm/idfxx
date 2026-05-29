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
 * The common case — resolve a host and use the first usable address — is served
 * by `resolve_one` / `try_resolve_one`. To examine several candidate addresses
 * (for example, to try each in turn until one connects) use `resolve_each`
 * / `try_resolve_each`, which visit results without allocating. `resolve` /
 * `try_resolve` collect every result into a `std::vector` for callers that want
 * the full set in hand.
 *
 * @note By default a single address is returned per query. Enable
 *       `CONFIG_LWIP_USE_ESP_GETADDRINFO` to receive multiple records
 *       (subject to `DNS_MAX_HOST_IP`).
 * @{
 */

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace idfxx::net {

/**
 * @headerfile <idfxx/net/resolver>
 * @brief Resolver hints used by the resolution functions.
 */
struct resolver_options {
    /** @brief Address family preference. `std::nullopt` accepts either family. */
    std::optional<address_family> family = std::nullopt;

    /** @brief Require the host to be a numeric address — skip the DNS lookup. */
    bool numeric_host = false;

    /** @brief Require the service to be a numeric port — skip the service-name lookup. */
    bool numeric_port = false;

    /** @brief Request results suitable for `bind()` rather than `connect()`. */
    bool passive = false;

    /** @brief Restrict results to a particular transport protocol. `std::nullopt` accepts any protocol. */
    std::optional<ip_protocol> protocol = std::nullopt;
};

/// @cond INTERNAL
namespace detail {

// Type-erased iteration over resolution results. `sink` is invoked for each
// resolved endpoint; returning false stops iteration early. The sink stopping
// early is not an error — the returned result reflects only the resolution
// itself. Backing the templated `resolve_each` keeps the lwIP-facing code
// out of the public header while preserving zero-allocation iteration.
[[nodiscard]] result<void> resolve_for_each(
    std::string_view host,
    port_number port,
    const resolver_options& opts,
    bool (*sink)(void* ctx, const endpoint&),
    void* ctx
);
[[nodiscard]] result<void> resolve_for_each(
    std::string_view host,
    std::string_view service,
    const resolver_options& opts,
    bool (*sink)(void* ctx, const endpoint&),
    void* ctx
);

template<typename F>
bool invoke_resolve_sink(void* ctx, const endpoint& ep) {
    F& fn = *static_cast<F*>(ctx);
    if constexpr (std::is_void_v<std::invoke_result_t<F&, const endpoint&>>) {
        fn(ep);
        return true;
    } else {
        return fn(ep);
    }
}

} // namespace detail
/// @endcond

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Resolves a host and numeric port and returns the first matching endpoint.
 *
 * @param host Host name or numeric address.
 * @param port Port number in host byte order.
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
 * @brief Resolves a host and numeric port and returns the first matching endpoint.
 *
 * @param host Host name or numeric address.
 * @param port Port number in host byte order.
 * @param opts Resolver options.
 * @return The first endpoint, or an error such as `errc::name_not_found`.
 */
[[nodiscard]] result<endpoint>
try_resolve_one(std::string_view host, port_number port, const resolver_options& opts = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Resolves a host and service name and returns the first matching endpoint.
 *
 * @param host    Host name or numeric address.
 * @param service Service name (e.g. `"http"`) or numeric port string.
 * @param opts    Resolver options.
 *
 * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
 * @throws std::system_error if no endpoint is found or on other failure.
 *
 * @return The first endpoint.
 */
[[nodiscard]] endpoint resolve_one(std::string_view host, std::string_view service, const resolver_options& opts = {});
#endif

/**
 * @brief Resolves a host and service name and returns the first matching endpoint.
 *
 * @param host    Host name or numeric address.
 * @param service Service name (e.g. `"http"`) or numeric port string.
 * @param opts    Resolver options.
 * @return The first endpoint, or an error such as `errc::name_not_found`.
 */
[[nodiscard]] result<endpoint>
try_resolve_one(std::string_view host, std::string_view service, const resolver_options& opts = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Visits each resolved endpoint for a host and numeric port without allocating.
 *
 * @tparam F Callable invoked as `f(const endpoint&)`. If it returns `void`, every
 *           result is visited. If it returns `bool`, returning `false` stops the
 *           iteration early (e.g. once a connection attempt has succeeded).
 *
 * @param host Host name or numeric address.
 * @param port Port number in host byte order.
 * @param f    Visitor applied to each resolved endpoint.
 * @param opts Resolver options.
 *
 * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
 * @throws std::system_error on resolution failure. The visitor stopping early is
 *         not a failure.
 *
 * @code
 * idfxx::net::resolve_each("example.com", 80, [](const idfxx::net::endpoint& ep) {
 *     idfxx::log::info("app", "candidate {}", ep);
 * });
 * @endcode
 */
template<typename F>
void resolve_each(std::string_view host, port_number port, F&& f, const resolver_options& opts = {}) {
    idfxx::unwrap(try_resolve_each(host, port, std::forward<F>(f), opts));
}
#endif

/**
 * @brief Visits each resolved endpoint for a host and numeric port without allocating.
 *
 * @tparam F Callable invoked as `f(const endpoint&)`. If it returns `void`, every
 *           result is visited. If it returns `bool`, returning `false` stops the
 *           iteration early (e.g. once a connection attempt has succeeded).
 *
 * @param host Host name or numeric address.
 * @param port Port number in host byte order.
 * @param f    Visitor applied to each resolved endpoint.
 * @param opts Resolver options.
 * @return Success once resolution completes (whether or not the visitor stopped
 *         early), or a resolution error.
 */
template<typename F>
[[nodiscard]] result<void>
try_resolve_each(std::string_view host, port_number port, F&& f, const resolver_options& opts = {}) {
    using fn_type = std::remove_reference_t<F>;
    return detail::resolve_for_each(host, port, opts, &detail::invoke_resolve_sink<fn_type>, std::addressof(f));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Visits each resolved endpoint for a host and service name without allocating.
 *
 * @tparam F Callable invoked as `f(const endpoint&)`. If it returns `void`, every
 *           result is visited. If it returns `bool`, returning `false` stops the
 *           iteration early.
 *
 * @param host    Host name or numeric address.
 * @param service Service name (e.g. `"http"`) or numeric port string.
 * @param f       Visitor applied to each resolved endpoint.
 * @param opts    Resolver options.
 *
 * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
 * @throws std::system_error on resolution failure. The visitor stopping early is
 *         not a failure.
 */
template<typename F>
void resolve_each(std::string_view host, std::string_view service, F&& f, const resolver_options& opts = {}) {
    idfxx::unwrap(try_resolve_each(host, service, std::forward<F>(f), opts));
}
#endif

/**
 * @brief Visits each resolved endpoint for a host and service name without allocating.
 *
 * @tparam F Callable invoked as `f(const endpoint&)`. If it returns `void`, every
 *           result is visited. If it returns `bool`, returning `false` stops the
 *           iteration early.
 *
 * @param host    Host name or numeric address.
 * @param service Service name (e.g. `"http"`) or numeric port string.
 * @param f       Visitor applied to each resolved endpoint.
 * @param opts    Resolver options.
 * @return Success once resolution completes (whether or not the visitor stopped
 *         early), or a resolution error.
 */
template<typename F>
[[nodiscard]] result<void>
try_resolve_each(std::string_view host, std::string_view service, F&& f, const resolver_options& opts = {}) {
    using fn_type = std::remove_reference_t<F>;
    return detail::resolve_for_each(host, service, opts, &detail::invoke_resolve_sink<fn_type>, std::addressof(f));
}

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
/**
 * @brief Resolves a host and numeric port and collects every endpoint.
 *
 * Prefer `resolve_one` for the common single-address case, or `resolve_each`
 * to iterate candidates without allocating; this overload exists for callers that
 * want the full result set in a container.
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
 * @brief Resolves a host and numeric port and collects every endpoint.
 *
 * Prefer `try_resolve_one` for the common single-address case, or
 * `try_resolve_each` to iterate candidates without allocating; this overload
 * exists for callers that want the full result set in a container.
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
 * @brief Resolves a host and service name and collects every endpoint.
 *
 * Prefer `resolve_one` for the common single-address case, or `resolve_each`
 * to iterate candidates without allocating; this overload exists for callers that
 * want the full result set in a container.
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
 * @brief Resolves a host and service name and collects every endpoint.
 *
 * Prefer `try_resolve_one` for the common single-address case, or
 * `try_resolve_each` to iterate candidates without allocating; this overload
 * exists for callers that want the full result set in a container.
 *
 * @param host    Host name or numeric address.
 * @param service Service name (e.g. `"http"`) or numeric port string.
 * @param opts    Resolver options.
 * @return List of resolved endpoints, or an error.
 */
[[nodiscard]] result<std::vector<endpoint>>
try_resolve(std::string_view host, std::string_view service, const resolver_options& opts = {});

} // namespace idfxx::net

/** @} */ // end of idfxx_net_resolver
