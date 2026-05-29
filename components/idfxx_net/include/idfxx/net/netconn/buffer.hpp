// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @headerfile <idfxx/net/netconn/buffer>
 * @file buffer.hpp
 * @brief Owning handle for a netconn buffer.
 *
 * @defgroup idfxx_net_netconn_buffer Buffer
 * @ingroup idfxx_net_netconn
 * @brief Owning handle for a netconn buffer.
 * @{
 */

#include "sdkconfig.h"

#include <idfxx/net/endpoint>
#include <idfxx/net/error>

#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>

// Forward declaration from lwIP.
struct netbuf;

namespace idfxx::net::netconn {

class stream_channel;
class datagram_channel;
class raw_channel;
namespace detail {
class connectionless_channel;
}

/**
 * @headerfile <idfxx/net/netconn/buffer>
 * @brief Owning handle for a netconn buffer.
 *
 * A buffer holds the data returned by `*_channel::try_recv` and, for UDP, the
 * source address and port. Data may span multiple segments — iterate
 * `segments()` to walk them without copying, or `copy_into()` to gather into
 * a contiguous buffer.
 *
 * Ownership is exclusive: the buffer is non-copyable and move-only. A
 * moved-from buffer may only be destroyed, move-assigned, or queried via
 * `is_open()` (→ `false`) and `idf_handle()` (→ `nullptr`); calling any other
 * method on it is undefined behavior. The destructor releases the held buffer.
 *
 * @warning `attach()` references external memory rather than copying it; the
 * referenced buffer must outlive the send (see the method documentation). To
 * send a transient buffer without that obligation, prefer the copying
 * `datagram_channel::try_send(std::span)` / `raw_channel::try_send(std::span)`
 * overloads, which manage the buffer's lifetime internally.
 */
class [[nodiscard]] buffer {
public:
    /**
     * @brief Allocates an empty buffer.
     *
     * @return The new buffer, or an error.
     */
    [[nodiscard]] static result<buffer> make();

    /** @brief Default-constructs an empty (non-owning) buffer handle. */
    buffer() noexcept = default;

    /** @brief Frees the buffer if owned. */
    ~buffer();

    buffer(const buffer&) = delete;
    buffer& operator=(const buffer&) = delete;

    /** @brief Move construction transfers ownership. */
    buffer(buffer&& other) noexcept;

    /** @brief Move assignment frees the current buffer and takes the other's. */
    buffer& operator=(buffer&& other) noexcept;

    /**
     * @brief Returns the underlying lwIP `netbuf*`, or `nullptr` if moved-from / empty.
     *
     * For observation and interop only: do not free or re-own the returned
     * netbuf — doing so desynchronizes this handle's invariants.
     */
    [[nodiscard]] ::netbuf* idf_handle() const noexcept { return _buf; }

    /** @brief Returns true if this buffer owns a netbuf (false after move-from). */
    [[nodiscard]] bool is_open() const noexcept { return _buf != nullptr; }

    /** @brief Returns the total length of all segments. */
    [[nodiscard]] size_t len() const noexcept;

    /**
     * @brief Returns the source endpoint for a UDP receive.
     *
     * @return The source endpoint, or `std::nullopt` if unavailable.
     */
    [[nodiscard]] std::optional<endpoint> from_endpoint() const noexcept;

    /**
     * @brief A forward range over a buffer's data segments.
     *
     * Each element is a `std::span<const std::byte>` referencing one segment of
     * the received data, in order. Iteration does not modify the buffer, so the
     * range may be traversed more than once and the buffer remains usable
     * afterwards.
     *
     * Obtained from `buffer::segments()`; not constructed directly.
     */
    class segment_view {
    public:
        /// @brief Forward iterator over a buffer's data segments.
        class iterator {
        public:
            using value_type = std::span<const std::byte>;
            using difference_type = std::ptrdiff_t;
            using iterator_concept = std::forward_iterator_tag;
            // operator* yields a prvalue span, so the C++17 category tops out at
            // input; iterator_concept above is what makes it a forward_iterator.
            using iterator_category = std::input_iterator_tag;

            /// @brief Constructs a past-the-end iterator.
            iterator() noexcept = default;

            /// @brief Returns the current segment as a span.
            [[nodiscard]] value_type operator*() const noexcept;

            /// @brief Advances to the next segment.
            iterator& operator++() noexcept;

            /// @brief Advances to the next segment, returning the prior position.
            iterator operator++(int) noexcept {
                auto tmp = *this;
                ++*this;
                return tmp;
            }

            /// @brief Compares two iterators; equal iff they reference the same segment.
            [[nodiscard]] bool operator==(const iterator& other) const noexcept { return _seg == other._seg; }

        private:
            friend class segment_view;
            explicit iterator(const void* seg) noexcept
                : _seg(seg) {}

            const void* _seg = nullptr;
        };

        /// @brief Returns an iterator to the first segment.
        [[nodiscard]] iterator begin() const noexcept;

        /// @brief Returns the past-the-end iterator.
        [[nodiscard]] iterator end() const noexcept { return iterator{}; }

    private:
        friend class buffer;
        explicit segment_view(const ::netbuf* buf) noexcept
            : _buf(buf) {}

        const ::netbuf* _buf = nullptr;
    };

    /**
     * @brief Returns a forward range over the buffer's data segments.
     *
     * Walk a multi-segment receive without copying:
     * @code
     * for (auto seg : buf.segments()) {
     *     // process seg (std::span<const std::byte>) without copying
     * }
     * @endcode
     *
     * The range is safe to obtain from a moved-from or empty buffer — it is
     * simply empty.
     *
     * @return A `segment_view` spanning the buffer's segments in order.
     */
    [[nodiscard]] segment_view segments() const noexcept { return segment_view{_buf}; }

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    /**
     * @brief Attaches an external (non-owned) buffer for sending.
     *
     * The data is referenced, not copied.
     *
     * @warning @p data must remain valid and unmodified until the buffer has
     *          been handed to and consumed by a send
     *          (`netconn::datagram_channel::try_send` / `try_send_to` or the raw
     *          equivalents). Sending a transient buffer is a use-after-free; for
     *          that case use the copying `*_channel::try_send(std::span)`
     *          overloads, which manage the buffer's lifetime internally.
     *
     * @param data Buffer to reference.
     *
     * @note Only available when `CONFIG_COMPILER_CXX_EXCEPTIONS` is enabled.
     * @throws std::system_error on failure.
     */
    void attach(std::span<const std::byte> data) { idfxx::unwrap(try_attach(data)); }
#endif

    /**
     * @brief Copies all segments into a destination buffer.
     *
     * @param dst Destination buffer.
     * @return Number of bytes copied (≤ `dst.size()`).
     */
    [[nodiscard]] size_t copy_into(std::span<std::byte> dst) const noexcept;

    /**
     * @brief Attaches an external (non-owned) buffer for sending.
     *
     * The data is referenced, not copied.
     *
     * @warning @p data must remain valid and unmodified until the buffer has
     *          been handed to and consumed by a send
     *          (`netconn::datagram_channel::try_send` / `try_send_to` or the raw
     *          equivalents). Sending a transient buffer is a use-after-free; for
     *          that case use the copying `*_channel::try_send(std::span)`
     *          overloads, which manage the buffer's lifetime internally.
     *
     * @param data Buffer to reference.
     * @return Success, or an error.
     */
    [[nodiscard]] result<void> try_attach(std::span<const std::byte> data);

private:
    friend class stream_channel;
    friend class datagram_channel;
    friend class raw_channel;
    friend class detail::connectionless_channel;

    explicit buffer(::netbuf* buf) noexcept
        : _buf(buf) {}

    ::netbuf* _buf = nullptr;
};

} // namespace idfxx::net::netconn

/// @cond INTERNAL
// A `segment_view` iterator points into the owning buffer's netbuf chain, not
// into the view object itself, so it stays valid after the view is destroyed
// (as long as the buffer lives). That makes the view a borrowed range: piping a
// temporary's segments through a view adaptor — `buf.segments() | views::…` —
// does not dangle.
template<>
inline constexpr bool std::ranges::enable_borrowed_range<idfxx::net::netconn::buffer::segment_view> = true;
/// @endcond

/** @} */ // end of idfxx_net_netconn_buffer
