// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @file line_ring.hpp
 * @brief A ring of log text in memory, which stays readable after a reset at any instant.
 *
 * @defgroup idfxx_persistent_log_line_ring Line ring
 * @brief Holds the log lines that are waiting to be written to flash, and the newest lines, which
 *        give a fault its context.
 *
 * A @ref idfxx::persistent_log::line_ring is a plain struct, so that it can live in memory that
 * survives deep sleep and the reset after a panic (`RTC_NOINIT_ATTR`). There it starts with
 * whatever the memory held. Text is appended at the ring's head. A writer takes the text that
 * hasn't been flushed yet, and marks it flushed once it is in flash. When the waiting text grows
 * larger than the ring, the oldest text is dropped and counted. The ring then tells the next flush
 * how many bytes were lost.
 *
 * A reset can come between any two stores of an append, and the ring must read back consistently
 * after the reset. So the indices that release old bytes move before the new bytes are copied over
 * the old ones. The head moves only after the copy. Bytes beyond the head are never trusted.
 *
 * @code
 * RTC_NOINIT_ATTR idfxx::persistent_log::line_ring<6144> ring;
 *
 * if (!ring.valid()) {
 *     ring.reset();
 * }
 * ring.append("12.345 +1234 - wake reason=timer\n");
 *
 * auto p = ring.pending();
 * // write p.first and p.second to flash (noting p.lost, if any were dropped), then:
 * ring.mark_flushed(p);
 * @endcode
 * @{
 */

#include "log_line.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace idfxx::persistent_log {

/**
 * @brief The steps of @ref line_ring::append. A reset may come at any of these steps.
 */
enum class append_step {
    /// The bytes about to be lost are counted in `dropped`.
    counted_dropped,
    /// `flushed` has moved past the unflushed bytes that the new bytes will overwrite.
    released_unflushed,
    /// `begin` has moved past the bytes that the new bytes will overwrite.
    released_oldest,
    /// The new bytes up to the end of `data` are copied.
    copied_part,
    /// All the new bytes are copied. The head hasn't moved yet.
    copied,
};

/**
 * @brief Up to two runs of bytes, in order, that together make a range of the ring.
 */
struct ring_bytes {
    /// The first run.
    std::span<const char> first;
    /// The run after the first, from the start of `data`. It is empty when the range doesn't
    /// wrap.
    std::span<const char> second;

    /** @brief Returns the number of bytes in both runs. */
    [[nodiscard]] size_t size() const noexcept { return first.size() + second.size(); }
};

/**
 * @brief The bytes not yet flushed, and how many bytes were lost before them, as returned by
 *        @ref line_ring::pending.
 *
 * Pass it to @ref line_ring::mark_flushed once the bytes are flushed.
 */
struct pending_bytes : ring_bytes {
    /// The head at the time of the @ref line_ring::pending call. The bytes end there.
    uint32_t end = 0;
    /// The number of bytes lost before they were flushed, since the last
    /// @ref line_ring::mark_flushed (or @ref line_ring::reset). When it is not 0, the bytes may
    /// begin in the middle of a line.
    uint32_t lost = 0;
};

/**
 * @brief A ring of @p N bytes of log text, with the indices of its oldest, unflushed and newest
 *        bytes.
 *
 * The indices count bytes appended since @ref reset, modulo the largest multiple of @p N that
 * fits in 32 bits, so that a byte's place in `data` is its index modulo @p N. The ring allocates no
 * memory and has no lock. Callers that append from several tasks serialize the calls themselves.
 *
 * @tparam N The size of the ring in bytes.
 */
template<size_t N>
struct line_ring {
    static_assert(N > 0 && N <= 0x40000000, "the ring must be between 1 byte and 1 GiB");

    /// What `magic` holds once the ring has been reset. It is "MLR2" with @p N XORed in, so that
    /// rings of different sizes have different magics.
    static constexpr uint32_t ring_magic = 0x32524C4D ^ static_cast<uint32_t>(N);

    /// The modulus of the indices.
    static constexpr uint64_t modulus = (uint64_t{1} << 32) / N * N;

    /// @ref ring_magic once the ring has been reset. After a power-on, it may hold any other
    /// value.
    uint32_t magic;
    /// The index of the oldest byte that can be trusted.
    uint32_t begin;
    /// The index of the oldest byte not yet flushed.
    uint32_t flushed;
    /// The index one past the newest byte.
    uint32_t head;
    /// The number of bytes lost before they were flushed, since the last reset.
    uint32_t dropped;
    /// The part of `dropped` that the last @ref mark_flushed (or @ref reset) accounted for.
    /// `dropped` minus this value is the number of lost bytes that no flush has been told about.
    /// Any value is consistent with the indices, so @ref valid can't check it.
    uint32_t dropped_at_flush;
    /// The bytes, each at its index modulo @p N.
    char data[N];

    /**
     * @brief Returns true when the ring holds a state that an append or a reset could have left.
     *
     * After a power-on, the memory is random. After a firmware update that changes the ring's
     * size, the memory may hold a ring of the old size. So the magic must match, and the indices
     * must be in order (`begin`, then `flushed`, then `head`) and span at most @p N bytes. The
     * magic depends on @p N, so a ring of another size is not valid.
     */
    [[nodiscard]] bool valid() const noexcept {
        if (magic != ring_magic || begin >= modulus || flushed >= modulus || head >= modulus) {
            return false;
        }
        uint32_t span = _distance(begin, head);
        return span <= N && _distance(begin, flushed) <= span;
    }

    /** @brief Empties the ring and marks it valid. */
    void reset() noexcept {
        // A reset in the middle of this leaves the magic wrong, so the next boot resets the ring
        // again.
        magic = 0;
        _fence();
        begin = 0;
        flushed = 0;
        head = 0;
        dropped = 0;
        dropped_at_flush = 0;
        _fence();
        magic = ring_magic;
    }

    /**
     * @brief Appends text.
     *
     * If the bytes not yet flushed and the new ones together exceed @p N, the oldest unflushed
     * bytes are dropped and counted in `dropped`. When the text is longer than @p N, only its last
     * @p N bytes are appended, and the rest are counted as dropped.
     *
     * @param text The bytes to append.
     */
    void append(std::string_view text) noexcept {
        append(text, [](append_step) noexcept {});
    }

    /**
     * @brief Appends text, calling @p checkpoint after each step.
     *
     * This is for tests. A checkpoint that throws stops the append at that step, as a reset would.
     *
     * @param text       The bytes to append.
     * @param checkpoint Called with each @ref append_step as it completes.
     */
    template<typename Checkpoint>
    void append(std::string_view text, Checkpoint&& checkpoint) {
        size_t lost = 0;
        if (text.size() > N) {
            lost = text.size() - N;
            text.remove_prefix(lost);
        }
        uint32_t len = static_cast<uint32_t>(text.size());

        // Release the bytes that the new bytes will overwrite, before overwriting them. `flushed`
        // moves before `begin`, so that `begin` is never past `flushed`.
        uint32_t waiting = _distance(flushed, head);
        uint32_t over = waiting + len > N ? waiting + len - static_cast<uint32_t>(N) : 0;
        if (lost + over > 0) {
            // The loss is counted first. So if a reset comes before `flushed` moves, too many lost
            // bytes are counted, but no loss is hidden.
            dropped += static_cast<uint32_t>(lost + over);
            _fence();
            checkpoint(append_step::counted_dropped);
        }
        if (over > 0) {
            flushed = _advance(flushed, over);
            _fence();
            checkpoint(append_step::released_unflushed);
        }
        uint32_t held = _distance(begin, head);
        if (held + len > N) {
            begin = _advance(begin, held + len - static_cast<uint32_t>(N));
            _fence();
            checkpoint(append_step::released_oldest);
        }

        size_t at = head % N;
        size_t first = len < N - at ? len : N - at;
        std::memcpy(data + at, text.data(), first);
        _fence();
        checkpoint(append_step::copied_part);
        std::memcpy(data, text.data() + first, len - first);
        _fence();
        checkpoint(append_step::copied);

        head = _advance(head, len);
    }

    /**
     * @brief Ends the last line, if it was cut short.
     *
     * A reset can come between the appends that make up a line. The ring's last byte is then in
     * the middle of that line, and the next line appended would continue it. If the ring holds any
     * bytes, and the last byte written (flushed or not) isn't `'\n'`, this appends a `'\n'`. Call
     * it at startup after @ref valid (or @ref reset), before appending the first new line.
     *
     * @code
     * if (!ring.valid()) {
     *     ring.reset();
     * }
     * ring.end_line();
     * @endcode
     */
    void end_line() noexcept {
        if (head != begin && data[_back(head, 1) % N] != '\n') {
            append("\n");
        }
    }

    /** @brief Returns the number of bytes not yet flushed. */
    [[nodiscard]] size_t unflushed() const noexcept { return _distance(flushed, head); }

    /**
     * @brief Returns the bytes not yet flushed, oldest first.
     *
     * It never returns more than @p N bytes, even from a ring that isn't @ref valid. The spans
     * point into `data`, and an append that drops unflushed bytes overwrites them. So copy the
     * bytes out under the same lock as the appends, before writing them anywhere slow.
     */
    [[nodiscard]] pending_bytes pending() const noexcept {
        pending_bytes p;
        static_cast<ring_bytes&>(p) = _range(flushed, _distance(flushed, head));
        p.end = head;
        p.lost = dropped - dropped_at_flush;
        return p;
    }

    /**
     * @brief Marks the bytes that @ref pending returned as flushed, and marks the loss that it
     *        reported as told.
     *
     * The ring then starts counting losses again. The next @ref pending reports in `lost` only the
     * bytes that were lost after @ref pending returned @p p and that @p p didn't hold. Appends may
     * drop bytes of @p p after @ref pending returns it. Those bytes were copied out with @p p, so
     * they aren't lost.
     *
     * A reset between the two stores of this call leaves the bytes unmarked, so they are flushed
     * again. A reset before the call has the same effect. Either way, the reset never hides a loss.
     *
     * @param p The value that an earlier @ref pending returned, with no @ref reset since. If
     *          appends have since dropped bytes past its end, `flushed` is already further on, and
     *          it stays there.
     */
    void mark_flushed(const pending_bytes& p) noexcept {
        uint32_t upto = p.end;
        bool moves = upto < modulus && _distance(flushed, upto) <= _distance(flushed, head);
        // The bytes of p that appends have released since p was returned. That is all of them if
        // `flushed` is past p's end, and otherwise the bytes up to `flushed`.
        uint32_t taken = static_cast<uint32_t>(p.size());
        if (moves) {
            taken = _distance(_back(upto, taken), flushed);
        }
        // Mark as told the loss that p reported, and the bytes of p dropped since. Never mark more
        // than the drops not yet told.
        uint64_t told = uint64_t{p.lost} + taken;
        uint32_t untold = dropped - dropped_at_flush;
        dropped_at_flush += told < untold ? static_cast<uint32_t>(told) : untold;
        _fence();
        if (moves) {
            flushed = upto;
        }
    }

    /**
     * @brief Returns the newest whole entries, up to @p n bytes in total.
     *
     * The newest bytes might begin in the middle of a line, or on a line that starts with a tab
     * and so continues the entry before it. In that case, the bytes returned begin instead at the
     * next line that doesn't start with a tab (see @ref idfxx_persistent_log_line). If there is no
     * such line, nothing is returned. The oldest byte that the ring holds counts as the start of a
     * line only while nothing has been overwritten since the last reset.
     *
     * @param n The largest number of bytes to return.
     */
    [[nodiscard]] ring_bytes tail(size_t n) const noexcept {
        uint32_t held = _clamp(_distance(begin, head));
        uint32_t count = n < held ? static_cast<uint32_t>(n) : held;
        uint32_t start = _back(head, count);
        // `begin` starts a line only where reset() put it. Once `begin` has moved, the byte before
        // it is gone. (Its index comes back to 0 once in `modulus` bytes, and a tail taken then may
        // begin in the middle of a line.)
        bool at_line = count < held ? data[_back(start, 1) % N] == '\n' : begin == 0;
        const auto at = [this, start](size_t i) {
            return data[_advance(start, static_cast<uint32_t>(i)) % N];
        };
        size_t skip = detail::entry_start(at, 0, count, at_line);
        if (skip == count) {
            return {};
        }
        return _range(
            _advance(start, static_cast<uint32_t>(skip)), count - static_cast<uint32_t>(skip)
        );
    }

private:
    static void _fence() noexcept { std::atomic_signal_fence(std::memory_order_seq_cst); }

    static uint32_t _advance(uint32_t index, uint32_t by) noexcept {
        return static_cast<uint32_t>((uint64_t{index} + by) % modulus);
    }

    static uint32_t _back(uint32_t index, uint32_t by) noexcept {
        return static_cast<uint32_t>((uint64_t{index} + modulus - by) % modulus);
    }

    static uint32_t _distance(uint32_t from, uint32_t to) noexcept {
        return static_cast<uint32_t>((uint64_t{to} + modulus - from) % modulus);
    }

    // Limits a count to N. A larger count comes only from a ring that isn't valid.
    static uint32_t _clamp(uint32_t count) noexcept {
        return count < N ? count : static_cast<uint32_t>(N);
    }

    [[nodiscard]] ring_bytes _range(uint32_t from, uint32_t count) const noexcept {
        count = _clamp(count);
        size_t at = from % N;
        size_t first = count < N - at ? count : N - at;
        return {
            .first = std::span<const char>(data + at, first),
            .second = std::span<const char>(data, count - first),
        };
    }
};

} // namespace idfxx::persistent_log

/** @} */
