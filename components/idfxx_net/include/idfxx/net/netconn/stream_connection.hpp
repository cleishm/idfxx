// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/stream_connection>
 * @file stream_connection.hpp
 * @brief TCP-only netconn connection.
 *
 * @defgroup idfxx_net_netconn_stream_connection Netconn Stream Connection
 * @ingroup idfxx_net_netconn
 * @brief A TCP connection over the netconn API.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/flags>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/detail/base_connection.hpp>
#include <idfxx/net/netconn/netbuf>

#include <cstddef>
#include <cstdint>
#include <span>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/stream_connection>
 * @brief Flags accepted by `stream_connection::try_write`.
 *
 * The default (no flags set) is zero-copy: the caller must keep the buffer
 * alive until the write completes. Set `copy` to take a copy instead.
 */
enum class write_flag : uint8_t {
    copy = 0x01,       ///< Copy the buffer; the caller may reuse it immediately.
    more = 0x02,       ///< Suppress the TCP PSH flag (allow more data to be coalesced).
    dont_block = 0x04, ///< Return immediately rather than waiting for buffer space.
};

} // namespace idfxx::net::netconn

/// @cond INTERNAL
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::net::netconn::write_flag> = true;
/// @endcond

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/stream_connection>
 * @brief A TCP netconn connection.
 *
 * Use `idfxx::net::netconn::listener` to accept inbound connections; this
 * class is for outbound client connections and for the per-connection
 * connection returned by the listener.
 *
 * Ownership is exclusive: the connection is non-copyable and move-only.
 */
class stream_connection : public detail::base_connection {
public:
    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP netconn with default address family (IPv4).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] stream_connection();
#endif

    /**
     * @brief Creates a TCP netconn with default address family (IPv4).
     *
     * @return The new connection, or an error.
     */
    [[nodiscard]] static result<stream_connection> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP netconn for the given address family.
     *
     * @param fam Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    [[nodiscard]] explicit stream_connection(enum family fam);
#endif

    /**
     * @brief Creates a TCP netconn for the given address family.
     *
     * @param fam Address family.
     * @return The new connection, or an error.
     */
    [[nodiscard]] static result<stream_connection> make(enum family fam);

    // ----- connect / close / shutdown -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Connects to the given remote endpoint.
     *
     * @param peer Remote endpoint to connect to.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void connect(const endpoint& peer) { idfxx::unwrap(try_connect(peer)); }
#endif

    /**
     * @brief Connects to the given remote endpoint.
     *
     * @param peer Remote endpoint to connect to.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_connect(const endpoint& peer);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Closes the netconn.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void close() { idfxx::unwrap(try_close()); }
#endif

    /**
     * @brief Closes the netconn.
     *
     * @return Success, or an error.
     */
    result<void> try_close();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Shuts down both directions on the netconn.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void shutdown() { idfxx::unwrap(try_shutdown()); }
#endif

    /**
     * @brief Shuts down both directions on the netconn.
     *
     * @return Success, or an error.
     */
    result<void> try_shutdown();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Shuts down one direction on the netconn.
     *
     * @param dir Direction to shut down.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void shutdown(direction dir) { idfxx::unwrap(try_shutdown(dir)); }
#endif

    /**
     * @brief Shuts down one direction on the netconn.
     *
     * @param dir Direction to shut down.
     * @return Success, or an error.
     */
    result<void> try_shutdown(direction dir);

    // ----- send / receive -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a buffer on the TCP netconn.
     *
     * In the default blocking mode the full buffer is sent or an error is
     * thrown. The returned byte count is informational and only diverges
     * from `data.size()` when `write_flag::dont_block` is set; for that
     * partial-write accounting use `try_write`.
     *
     * @param data  Data to send.
     * @param flags Flags controlling copy behaviour and PSH suppression.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Number of bytes accepted.
     */
    size_t write(std::span<const std::byte> data, idfxx::flags<write_flag> flags = {}) {
        return idfxx::unwrap(try_write(data, flags));
    }
#endif

    /**
     * @brief Sends a buffer on the TCP netconn.
     *
     * @param data  Data to send.
     * @param flags Flags controlling copy behaviour and PSH suppression.
     * @return Number of bytes accepted, or an error.
     */
    [[nodiscard]] result<size_t> try_write(std::span<const std::byte> data, idfxx::flags<write_flag> flags = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives the next netbuf from the connection.
     *
     * On graceful peer close the returned netbuf is non-owning
     * (`is_open() == false`); this is the loop termination signal and is not
     * an error.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The received netbuf, or an empty netbuf on graceful close.
     */
    [[nodiscard]] netbuf recv() { return idfxx::unwrap(try_recv()); }
#endif

    /**
     * @brief Receives the next netbuf from the connection.
     *
     * @return The received netbuf (empty on graceful close), or an error.
     */
    [[nodiscard]] result<netbuf> try_recv();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives data into a caller-provided buffer.
     *
     * On graceful peer close the returned span is empty; this is the loop
     * termination signal and is not an error.
     *
     * @param buf Destination buffer.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Sub-span of @p buf covering the bytes actually received, or an
     *         empty span on graceful close.
     */
    [[nodiscard]] std::span<std::byte> recv(std::span<std::byte> buf) { return idfxx::unwrap(try_recv(buf)); }
#endif

    /**
     * @brief Receives data into a caller-provided buffer.
     *
     * @param buf Destination buffer.
     * @return Sub-span of @p buf covering the bytes actually received (empty on
     *         graceful close), or an error.
     */
    [[nodiscard]] result<std::span<std::byte>> try_recv(std::span<std::byte> buf);

    /// @cond INTERNAL
    /** @brief Constructs a stream_connection taking ownership of a `netconn*`. */
    stream_connection(::netconn* conn, enum family fam) noexcept
        : base_connection(conn, fam) {}
    /// @endcond

private:
    // Once the peer closes the receive direction (ERR_CLSD), lwIP only
    // signals that condition on the first `netconn_recv`; subsequent calls
    // return ERR_CONN. Track the close locally so `try_recv` keeps reporting
    // the BSD-style empty-netbuf EOF instead of surfacing the stale error.
    bool _eof = false;
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_stream_connection
