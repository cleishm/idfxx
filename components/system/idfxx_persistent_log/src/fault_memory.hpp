// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

/**
 * @file fault_memory.hpp
 * @brief The kinds of fault whose repeats are suppressed, so that a repeat isn't recorded again.
 *
 * @defgroup idfxx_persistent_log_fault_memory Fault memory
 * @brief Records which kinds of fault are suppressed, so that a later fault of a suppressed kind
 *        is only logged.
 *
 * A fault that keeps happening, such as a card that won't mount or a panel that won't draw, comes
 * again at every try. If it were recorded each time, it would fill the fault store with copies of
 * itself. A @ref idfxx::persistent_log::fault_memory holds the kinds of the faults that were
 * recorded with their repeats suppressed. It stores each kind as a CRC, and it holds up to as many
 * kinds as it is sized for. Any fault of a kind that it holds is a repeat, whatever the fault's
 * detail, and the caller only logs it. When the suppression of a kind is lifted, the caller forgets
 * the kind, and the next fault of that kind is recorded again.
 *
 * It is a plain struct of words, so that it can live in memory that survives deep sleep
 * (`RTC_NOINIT_ATTR`). There it sits inside a larger struct, which a CRC of that struct's words
 * covers. It starts with whatever the memory held. Call @ref fault_memory::clear when that can't
 * be trusted.
 *
 * @code
 * // The caller checks that it is valid.
 * RTC_NOINIT_ATTR idfxx::persistent_log::fault_memory<8> memory;
 *
 * // A new boot
 * memory.clear();
 *
 * // Recording a fault with its repeats suppressed (under the caller's lock)
 * if (memory.repeats(crc(kind))) {
 *     say("fault repeat kind={}", kind);
 * } else if (record(kind, detail)) {
 *     memory.remember(crc(kind));
 * }
 *
 * // Lifting the suppression
 * if (memory.holds(crc(kind))) { // without the lock
 *     memory.forget(crc(kind));  // with the lock held
 * }
 * @endcode
 * @{
 */

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace idfxx::persistent_log {

/**
 * @brief A suppressed kind. It holds the CRC of the kind, and when the kind was last recorded.
 *
 * A kind of 0 marks an empty slot. So a kind whose CRC is 0 can't be told apart from an empty slot
 * (the CRC of the empty kind is 0). See @ref fault_memory.
 */
struct remembered_fault {
    /// The CRC of the kind, or 0 for an empty slot.
    uint32_t kind;
    /// When the kind was last recorded, as the @ref fault_memory::next_order value that it took.
    uint32_t order;
};

/**
 * @brief The kinds of fault whose repeats are suppressed, as CRCs of the kinds.
 *
 * @tparam N The most kinds remembered at once. It must be at least 1.
 *
 * It holds up to @ref capacity kinds. When every slot is taken, a new kind takes the slot of the
 * kind recorded longest ago. It allocates no memory and has no lock. Callers serialize the calls
 * that change it, and they may call @ref holds without the lock.
 *
 * A kind is matched by its CRC, and 0 marks an empty slot. So a kind whose CRC is 0 matches an
 * empty slot. Whenever a slot is empty, such a kind is held and is a repeat. When it is
 * remembered, it is stored in a slot that still reads as empty.
 */
template<size_t N>
    requires(N > 0)
struct fault_memory {
    /// The most kinds remembered at once.
    static constexpr size_t capacity = N;

    /// The remembered kinds, each in a slot of its own, and the empty slots.
    remembered_fault slots[capacity];
    /// The order that the next remembered kind takes. Each order is one more than the one before
    /// it, counting from 0 since the last @ref clear.
    uint32_t next_order;

    /**
     * @brief Returns true when a fault of the kind is a repeat.
     *
     * A fault is a repeat when its kind is remembered, whatever the fault's detail.
     *
     * @param kind The CRC of the fault's kind.
     */
    [[nodiscard]] bool repeats(uint32_t kind) const noexcept { return _find(kind) != nullptr; }

    /**
     * @brief Remembers the kind of a recorded fault.
     *
     * The kind goes in the first empty slot. If no slot is empty, it goes in the slot of the kind
     * recorded longest ago. A kind that is already remembered keeps its slot. In every case, the
     * kind takes the next order, so that it is now the kind recorded most recently.
     *
     * @param kind The CRC of the fault's kind.
     */
    void remember(uint32_t kind) noexcept {
        remembered_fault* slot = _find(kind);
        if (slot == nullptr) {
            slot = &slots[0];
            for (auto& r : slots) {
                if (r.kind == 0) {
                    slot = &r;
                    break;
                }
                if (r.order < slot->order) {
                    slot = &r;
                }
            }
        }
        *slot = {.kind = kind, .order = next_order++};
    }

    /**
     * @brief Forgets a kind, if it is remembered, so that the next fault of the kind is not a
     *        repeat.
     *
     * This empties the kind's slot. The order keeps counting from where it was.
     *
     * @param kind The CRC of the kind.
     *
     * @return True if the kind was remembered.
     */
    bool forget(uint32_t kind) noexcept {
        remembered_fault* r = _find(kind);
        if (r == nullptr) {
            return false;
        }
        *r = {};
        return true;
    }

    /**
     * @brief Returns true when the kind is remembered.
     *
     * This is the same check as @ref repeats. It reads only each slot's kind, which is a single
     * word. So it may be called without the lock that serializes the calls that change the
     * memory. A word read while it is being written reads as either its old or its new value. The
     * caller checks again with the lock held (@ref forget) before it acts on the result.
     *
     * @param kind The CRC of the kind.
     */
    [[nodiscard]] bool holds(uint32_t kind) const noexcept { return _find(kind) != nullptr; }

    /** @brief Forgets every kind and starts the order again from 0. It writes every word. */
    void clear() noexcept {
        for (auto& r : slots) {
            r = {};
        }
        next_order = 0;
    }

private:
    [[nodiscard]] const remembered_fault* _find(uint32_t kind) const noexcept {
        for (const auto& r : slots) {
            if (r.kind == kind) {
                return &r;
            }
        }
        return nullptr;
    }

    [[nodiscard]] remembered_fault* _find(uint32_t kind) noexcept {
        return const_cast<remembered_fault*>(std::as_const(*this)._find(kind));
    }
};

/// Checks that a fault_memory<N> is the plain struct of words that its uses rely on. Returns true
/// when it is.
template<size_t N>
consteval bool check_fault_memory() {
    using memory = fault_memory<N>;
    // A dynamic initializer would wipe it on every boot.
    static_assert(std::is_trivially_default_constructible_v<memory>);
    // It holds only words, with no padding, so that a CRC over its bytes covers only what it
    // holds.
    static_assert(std::has_unique_object_representations_v<memory>);
    static_assert(std::is_standard_layout_v<memory>);
    static_assert(sizeof(memory) == (2 * N + 1) * sizeof(uint32_t));
    static_assert(alignof(memory) == alignof(uint32_t));
    return true;
}

static_assert(sizeof(remembered_fault) == 2 * sizeof(uint32_t));
// The fewest and the most kinds that the suppressed-faults option allows, and the option's
// default.
static_assert(check_fault_memory<1>());
static_assert(check_fault_memory<8>());
static_assert(check_fault_memory<32>());

} // namespace idfxx::persistent_log

/** @} */
