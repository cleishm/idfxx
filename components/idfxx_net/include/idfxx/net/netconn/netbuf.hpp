// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/netbuf>
 * @file netbuf.hpp
 * @brief Owning handle for a received netconn buffer.
 *
 * @defgroup idfxx_net_netconn_netbuf Netbuf
 * @ingroup idfxx_net_netconn
 * @brief Owning handle for a received netconn buffer.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <cstddef>
#include <optional>
#include <span>

// Forward declaration from lwIP.
struct netbuf;

namespace idfxx::net::netconn {

/**
 * @headerfile <idfxx/net/netconn/netbuf>
 * @brief Owning handle for a received network buffer.
 *
 * A netbuf holds the data returned by `*_connection::try_recv` and, for UDP,
 * the source address and port. Data may span multiple segments — use
 * `try_data()` and `advance()` to walk them, or `copy_into()` to gather
 * into a contiguous buffer.
 *
 * Ownership is exclusive: the netbuf is non-copyable and move-only. The
 * destructor releases the held buffer.
 */
class netbuf {
public:
    /**
     * @brief Allocates an empty netbuf.
     *
     * @return The new netbuf, or an error.
     */
    [[nodiscard]] static result<netbuf> make();

    /** @brief Default-constructs an empty (non-owning) netbuf handle. */
    netbuf() noexcept = default;

    /** @brief Frees the netbuf if owned. */
    ~netbuf();

    netbuf(const netbuf&) = delete;
    netbuf& operator=(const netbuf&) = delete;

    /** @brief Move construction transfers ownership. */
    netbuf(netbuf&& other) noexcept;

    /** @brief Move assignment frees the current buffer and takes the other's. */
    netbuf& operator=(netbuf&& other) noexcept;

    /** @brief Returns the underlying `netbuf*`, or `nullptr` if empty. */
    [[nodiscard]] ::netbuf* idf_handle() const noexcept { return _buf; }

    /** @brief Returns true if this netbuf owns a buffer (false after move-from). */
    [[nodiscard]] bool is_open() const noexcept { return _buf != nullptr; }

    /** @brief Returns the total length of all segments. */
    [[nodiscard]] size_t len() const noexcept;

    /**
     * @brief Steps to the next data segment.
     *
     * @return True on success, false if there are no more segments.
     */
    [[nodiscard]] bool advance() noexcept;

    /** @brief Resets the current segment pointer to the first segment. */
    void rewind() noexcept;

    /**
     * @brief Returns the source endpoint for a UDP receive.
     *
     * @return The source endpoint, or `std::nullopt` if unavailable.
     */
    [[nodiscard]] std::optional<endpoint> from_endpoint() const noexcept;

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Returns the data pointer and length for the current segment.
     *
     * Use `advance()` to step to the next segment.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     *
     * @return A span over the current segment.
     */
    [[nodiscard]] std::span<const std::byte> data() const { return idfxx::unwrap(try_data()); }

    /**
     * @brief Attaches an external (non-owned) buffer for sending.
     *
     * The caller must keep @p data alive until the netbuf is sent.
     *
     * @param data Buffer to reference.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void attach(std::span<const std::byte> data) { idfxx::unwrap(try_attach(data)); }
#endif

    /**
     * @brief Returns the data pointer and length for the current segment.
     *
     * Use `advance()` to step to the next segment.
     *
     * @return A span over the current segment, or an error.
     */
    [[nodiscard]] result<std::span<const std::byte>> try_data() const;

    /**
     * @brief Copies all segments into a destination buffer.
     *
     * @warning Calling on a moved-from netbuf is undefined behavior.
     *
     * @param dst Destination buffer.
     * @return Number of bytes copied (≤ `dst.size()`).
     */
    [[nodiscard]] size_t copy_into(std::span<std::byte> dst) const noexcept;

    /**
     * @brief Attaches an external (non-owned) buffer for sending.
     *
     * The caller must keep @p data alive until the netbuf is sent.
     *
     * @param data Buffer to reference.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_attach(std::span<const std::byte> data);

    /// @cond INTERNAL
    /**
     * @brief Constructs a netbuf taking ownership of an existing handle.
     */
    explicit netbuf(::netbuf* buf) noexcept
        : _buf(buf) {}
    /// @endcond

private:
    ::netbuf* _buf = nullptr;
};

} // namespace idfxx::net::netconn

/** @} */ // end of idfxx_net_netconn_netbuf
