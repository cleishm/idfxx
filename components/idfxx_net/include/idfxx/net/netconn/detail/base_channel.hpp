// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/// @cond INTERNAL

// Internal base class shared by the netconn channel types
// (`stream_channel`, `datagram_channel`, `raw_channel`, `listener`). Holds
// the owning `::netconn*` pointer, the address family, and the family-generic
// options that belong on every type.

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <chrono>
#include <cstdint>
#include <optional>

// Forward declaration from lwIP.
struct netconn;

namespace idfxx::net::netconn::detail {

/**
 * @brief Allocates a netconn of the given base type (`NETCONN_TCP`, `NETCONN_UDP`, etc.)
 *        and address family. Returns `errc::no_buffer_space` on lwIP allocation failure.
 */
[[nodiscard]] result<::netconn*> make_netconn(int base_type, address_family fam);

/**
 * @brief Allocates a raw netconn of the given base type, IP protocol, and address family.
 *        Returns `errc::no_buffer_space` on lwIP allocation failure.
 */
[[nodiscard]] result<::netconn*> make_netconn_with_proto(int base_type, uint8_t proto, address_family fam);

class base_channel {
public:
    /** @brief Deletes the netconn if owned. */
    ~base_channel() noexcept;

    base_channel(const base_channel&) = delete;
    base_channel& operator=(const base_channel&) = delete;

    base_channel(base_channel&& other) noexcept;
    base_channel& operator=(base_channel&& other) noexcept;

    /**
     * @brief Returns the underlying `netconn*`, or `nullptr` if moved-from / closed.
     *
     * For observation and interop only: do not close, free, or reconfigure the
     * returned netconn — doing so desynchronizes this wrapper's invariants.
     */
    [[nodiscard]] ::netconn* idf_handle() const noexcept { return _conn; }

    /** @brief Returns the address family this channel was created with. */
    [[nodiscard]] address_family family() const noexcept { return _family; }

    /** @brief Returns true if the channel is open (false after move-from). */
    [[nodiscard]] bool is_open() const noexcept { return _conn != nullptr; }

    /**
     * @brief Returns the local endpoint the channel is bound to.
     *
     * @return The bound local endpoint, or `std::nullopt` if the channel is
     *         moved-from / closed or the address could not be retrieved.
     */
    [[nodiscard]] std::optional<endpoint> local_endpoint() const noexcept;

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

    /** @brief Returns the current receive timeout in milliseconds (0 if no timeout is set). */
    [[nodiscard]] int get_recv_timeout() const noexcept;

protected:
    base_channel() noexcept = default;
    base_channel(::netconn* conn, address_family fam) noexcept
        : _conn(conn)
        , _family(fam) {}

    // Queries the netconn for its local (local == true) or peer (local ==
    // false) address. Shared by `local_endpoint` and the derived channels'
    // `peer_endpoint`. Returns `errc::invalid_state` on a moved-from channel.
    [[nodiscard]] result<endpoint> _try_getaddr(bool local) const;

    void _set_recv_timeout(std::chrono::milliseconds t) noexcept;

    ::netconn* _conn = nullptr;
    address_family _family = address_family::ipv4;
};

} // namespace idfxx::net::netconn::detail

/// @endcond
