// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/// @cond INTERNAL

// Internal base class shared by `datagram_socket` and `raw_socket`. Adds the
// per-message send/receive and disconnect operations that connectionless
// sockets share. Stream sockets use the `ip_socket_base` API directly.

#include "sdkconfig.h"

#include <idfxx/net/detail/ip_socket_base.hpp>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <cstddef>
#include <span>

namespace idfxx::net::detail {

class connectionless_socket_base : public ip_socket_base {
public:
#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Clears the default peer set by `connect`.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void disconnect() { idfxx::unwrap(try_disconnect()); }
#endif

    /**
     * @brief Clears the default peer set by `try_connect`.
     *
     * @return Success, or an error.
     */
    result<void> try_disconnect();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a datagram to a specific peer.
     *
     * @param buf Data to send.
     * @param to  Destination endpoint.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void send_to(std::span<const std::byte> buf, const endpoint& to) { idfxx::unwrap(try_send_to(buf, to)); }
#endif

    /**
     * @brief Sends a datagram to a specific peer.
     *
     * @param buf Data to send.
     * @param to  Destination endpoint.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_send_to(std::span<const std::byte> buf, const endpoint& to);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives a datagram and reports the sender's endpoint.
     *
     * @param buf Buffer to fill.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Datagram metadata (filled sub-span, source endpoint).
     */
    [[nodiscard]] datagram recv_from(std::span<std::byte> buf) { return idfxx::unwrap(try_recv_from(buf)); }
#endif

    /**
     * @brief Receives a datagram and reports the sender's endpoint.
     *
     * @param buf Buffer to fill.
     * @return Datagram metadata (filled sub-span, source endpoint), or an error.
     */
    [[nodiscard]] result<datagram> try_recv_from(std::span<std::byte> buf);

protected:
    using ip_socket_base::ip_socket_base;
};

} // namespace idfxx::net::detail

/// @endcond
