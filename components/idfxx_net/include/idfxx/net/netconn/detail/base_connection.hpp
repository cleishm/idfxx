// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/// @cond INTERNAL

// Internal base class shared by the netconn connection types
// (`stream_connection`, `datagram_connection`, `raw_connection`, `listener`).
// Holds the owning `::netconn*` pointer, the address family, and the
// family-generic options that belong on every type.

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstdint>

// Forward declaration from lwIP.
struct netconn;

namespace idfxx::net::netconn::detail {

/**
 * @brief Allocates a netconn of the given base type (`NETCONN_TCP`, `NETCONN_UDP`, etc.)
 *        and address family. Calls `raise_no_mem()` on lwIP allocation failure.
 */
[[nodiscard]] result<::netconn*> make_netconn(int base_type, enum family fam);

/**
 * @brief Allocates a raw netconn of the given base type, IP protocol, and address family.
 *        Calls `raise_no_mem()` on lwIP allocation failure.
 */
[[nodiscard]] result<::netconn*> make_netconn_with_proto(int base_type, uint8_t proto, enum family fam);

class base_connection {
public:
    /** @brief Deletes the netconn if owned. */
    ~base_connection() noexcept;

    base_connection(const base_connection&) = delete;
    base_connection& operator=(const base_connection&) = delete;

    base_connection(base_connection&& other) noexcept;
    base_connection& operator=(base_connection&& other) noexcept;

    /** @brief Returns the underlying `netconn*`, or `nullptr` if empty. */
    [[nodiscard]] ::netconn* idf_handle() const noexcept { return _conn; }

    /** @brief Returns the address family this connection was created with. */
    [[nodiscard]] enum family family() const noexcept { return _family; }

    /** @brief Returns true if the connection is open (false after move-from). */
    [[nodiscard]] bool is_open() const noexcept { return _conn != nullptr; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Binds the netconn to a local endpoint.
     *
     * @param addr Local endpoint to bind to.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void bind(const endpoint& addr) { idfxx::unwrap(try_bind(addr)); }
#endif

    /** @brief Binds the netconn to a local endpoint. */
    [[nodiscard]] result<void> try_bind(const endpoint& addr);

    /**
     * @brief Sets the maximum time `recv` will block waiting for data.
     *
     * @param t Receive timeout. Sub-millisecond resolutions are rounded up.
     */
    template<typename Rep, typename Period>
    void set_recv_timeout(const std::chrono::duration<Rep, Period>& t) noexcept {
        _set_recv_timeout(std::chrono::ceil<std::chrono::milliseconds>(t));
    }

    /**
     * @brief Toggles non-blocking mode — when on, recv/send return immediately when no
     *        data or buffer space is available rather than blocking the calling task.
     *
     * @param on Whether to enable non-blocking mode.
     */
    void set_non_blocking(bool on) noexcept;

    /**
     * @brief Returns the current receive timeout in milliseconds (0 if no timeout is set).
     *
     * @warning Calling on a moved-from connection is undefined behavior.
     */
    [[nodiscard]] int get_recv_timeout() const noexcept;

protected:
    base_connection() noexcept = default;
    base_connection(::netconn* conn, enum family fam) noexcept
        : _conn(conn)
        , _family(fam) {}

    void _set_recv_timeout(std::chrono::milliseconds t) noexcept;

    ::netconn* _conn = nullptr;
    enum family _family = family::ipv4;
};

} // namespace idfxx::net::netconn::detail

/// @endcond
