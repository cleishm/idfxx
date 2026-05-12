// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/stream_channel>
 * @file stream_channel.hpp
 * @brief TCP netconn channel.
 *
 * @defgroup idfxx_net_netconn_stream_channel Netconn Stream Channel
 * @ingroup idfxx_net_netconn
 * @brief A TCP channel over the netconn API.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/flags>
#include <idfxx/net/endpoint>
#include <idfxx/net/error>
#include <idfxx/net/netconn/buffer>
#include <idfxx/net/netconn/detail/base_channel.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/stream_channel>
 * @brief Flags accepted by `stream_channel::send` / `try_send`.
 *
 * The default (no flags set) copies the data into the stack's buffers, so the
 * caller may reuse or destroy its buffer as soon as the call returns. Set
 * `no_copy` to reference the caller's buffer instead — see the lifetime
 * warning on that flag.
 */
enum class write_flag : uint8_t {
    /// Zero-copy: reference the caller's buffer rather than copying it.
    /// @warning The referenced memory must remain valid not just until the
    /// call returns, but until the data has been transmitted *and
    /// acknowledged* by the peer — `try_send` returns once the data is
    /// enqueued, while the stack continues to hold the reference for
    /// retransmission. Use only for storage that outlives the connection
    /// (e.g. `static` or flash-resident data); never for stack or
    /// soon-freed buffers.
    no_copy = 0x01,
    more = 0x02,       ///< Suppress the TCP PSH flag (allow more data to be coalesced).
    dont_block = 0x04, ///< Return immediately rather than waiting for buffer space.
};

} // namespace idfxx::net::netconn

/// @cond INTERNAL
template<>
inline constexpr bool idfxx::enable_flags_operators<idfxx::net::netconn::write_flag> = true;
/// @endcond

namespace idfxx::net::netconn {

class listener;

/**
 * @headerfile <idfxx/net/netconn/stream_channel>
 * @brief A TCP netconn channel.
 *
 * Use `idfxx::net::netconn::listener` to accept inbound connections; this
 * class is for outbound client connections and for the per-connection channel
 * returned by the listener.
 *
 * Ownership is exclusive: the channel is non-copyable and move-only. A
 * moved-from channel may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `nullptr`); calling any other
 * method on it is undefined behavior. The destructor releases the channel if
 * open.
 */
class [[nodiscard]] stream_channel : public detail::base_channel {
public:
    // ----- construction -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP netconn with default address family (IPv4).
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    stream_channel();
#endif

    /**
     * @brief Creates a TCP netconn with default address family (IPv4).
     *
     * @return The new channel, or an error.
     */
    [[nodiscard]] static result<stream_channel> make();

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Creates a TCP netconn for the given address family.
     *
     * @param fam Address family.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    explicit stream_channel(address_family fam);
#endif

    /**
     * @brief Creates a TCP netconn for the given address family.
     *
     * @param fam Address family.
     * @return The new channel, or an error.
     */
    [[nodiscard]] static result<stream_channel> make(address_family fam);

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
     * @brief Shuts down one direction on the netconn (half-close).
     *
     * The two directions are independent: after `shutdown(direction::send)` the
     * peer sees end-of-stream but `recv` keeps returning data the peer sends
     * until it closes; after `shutdown(direction::receive)` further reads return
     * the empty-buffer close signal while sends still succeed.
     *
     * @param dir Direction to shut down.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void shutdown(direction dir) { idfxx::unwrap(try_shutdown(dir)); }
#endif

    /**
     * @brief Shuts down one direction on the netconn (half-close).
     *
     * The two directions are independent: after `shutdown(direction::send)` the
     * peer sees end-of-stream but `recv` keeps returning data the peer sends
     * until it closes; after `shutdown(direction::receive)` further reads return
     * the empty-buffer close signal while sends still succeed.
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
     * By default the data is copied into the stack's buffers, so @p data may be
     * reused or destroyed as soon as this call returns. Pass `write_flag::no_copy`
     * to reference the caller's buffer instead.
     *
     * In the default blocking mode the full buffer is sent or an error is
     * thrown. The returned byte count is informational and only diverges
     * from `data.size()` when `write_flag::dont_block` is set; for that
     * partial-write accounting use `try_send`.
     *
     * @warning With `write_flag::no_copy` set, @p data must remain valid and
     *          unmodified until the data has been transmitted *and acknowledged*
     *          by the peer — not merely until this call returns. Use only for
     *          storage that outlives the connection (e.g. `static` or
     *          flash-resident data).
     *
     * @param data  Data to send.
     * @param flags Flags controlling copy behaviour and PSH suppression.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Number of bytes accepted.
     */
    size_t send(std::span<const std::byte> data, idfxx::flags<write_flag> flags = {}) {
        return idfxx::unwrap(try_send(data, flags));
    }
#endif

    /**
     * @brief Sends a buffer on the TCP netconn.
     *
     * By default the data is copied into the stack's buffers, so @p data may be
     * reused or destroyed as soon as this call returns. Pass `write_flag::no_copy`
     * to reference the caller's buffer instead.
     *
     * @warning With `write_flag::no_copy` set, @p data must remain valid and
     *          unmodified until the data has been transmitted *and acknowledged*
     *          by the peer — not merely until this call returns. Use only for
     *          storage that outlives the connection (e.g. `static` or
     *          flash-resident data).
     *
     * @param data  Data to send.
     * @param flags Flags controlling copy behaviour and PSH suppression.
     * @return Number of bytes accepted, or an error.
     */
    [[nodiscard]] result<size_t> try_send(std::span<const std::byte> data, idfxx::flags<write_flag> flags = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Sends a string view on the TCP netconn.
     *
     * By default the data is copied into the stack's buffers, so @p data may be
     * reused or destroyed as soon as this call returns. Pass `write_flag::no_copy`
     * to reference the caller's buffer instead.
     *
     * In the default blocking mode the full buffer is sent or an error is
     * thrown. The returned byte count is informational and only diverges
     * from `data.size()` when `write_flag::dont_block` is set; for that
     * partial-write accounting use `try_send`.
     *
     * @warning With `write_flag::no_copy` set, @p data must remain valid and
     *          unmodified until the data has been transmitted *and acknowledged*
     *          by the peer — not merely until this call returns.
     *
     * @param data  Data to send.
     * @param flags Flags controlling copy behaviour and PSH suppression.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return Number of bytes accepted.
     */
    size_t send(std::string_view data, idfxx::flags<write_flag> flags = {}) {
        return idfxx::unwrap(try_send(data, flags));
    }
#endif

    /**
     * @brief Sends a string view on the TCP netconn.
     *
     * By default the data is copied into the stack's buffers, so @p data may be
     * reused or destroyed as soon as this call returns. Pass `write_flag::no_copy`
     * to reference the caller's buffer instead.
     *
     * @warning With `write_flag::no_copy` set, @p data must remain valid and
     *          unmodified until the data has been transmitted *and acknowledged*
     *          by the peer — not merely until this call returns.
     *
     * @param data  Data to send.
     * @param flags Flags controlling copy behaviour and PSH suppression.
     * @return Number of bytes accepted, or an error.
     */
    [[nodiscard]] result<size_t> try_send(std::string_view data, idfxx::flags<write_flag> flags = {});

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Receives the next buffer from the connection.
     *
     * On graceful peer close the returned buffer is non-owning
     * (`is_open() == false`); this is the loop termination signal and is not
     * an error.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return The received buffer, or an empty buffer on graceful close.
     */
    [[nodiscard]] buffer recv() { return idfxx::unwrap(try_recv()); }
#endif

    /**
     * @brief Receives the next buffer from the connection.
     *
     * @return The received buffer (empty on graceful close), or an error.
     */
    [[nodiscard]] result<buffer> try_recv();

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

    // ----- endpoint -----

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the remote endpoint the channel is connected to.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure (e.g. the channel is not connected).
     *
     * @return The peer's endpoint.
     */
    [[nodiscard]] endpoint peer_endpoint() const { return idfxx::unwrap(try_peer_endpoint()); }
#endif

    /**
     * @brief Returns the remote endpoint the channel is connected to.
     *
     * @return The peer's endpoint, or an error.
     */
    [[nodiscard]] result<endpoint> try_peer_endpoint() const { return _try_getaddr(false); }

private:
    friend class listener;

    stream_channel(::netconn* conn, address_family fam) noexcept
        : base_channel(conn, fam) {}

    // Once the peer closes the receive direction (ERR_CLSD), lwIP only
    // signals that condition on the first `netconn_recv`; subsequent calls
    // return ERR_CONN. Track the close locally so `try_recv` keeps reporting
    // the BSD-style empty-buffer EOF instead of surfacing the stale error.
    bool _eof = false;

    // Leftover from a previous `try_recv(std::span)` whose caller buffer was
    // smaller than the segment lwIP delivered. The netbuf-returning
    // `try_recv` has no partial-consume hook on the lwIP side, so when the
    // copy overshoots we keep the netbuf alive here and drain the remainder
    // on subsequent span-recv calls. Cleared on close/shutdown and moved
    // along with the channel.
    buffer _pending;
    size_t _pending_offset = 0;
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_stream_channel
