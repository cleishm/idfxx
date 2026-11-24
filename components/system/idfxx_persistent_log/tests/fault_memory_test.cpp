// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_persistent_log's fault memory. The fault memory tells a fault of a
// suppressed kind apart from a fault of a kind that isn't suppressed. It holds one kind in each
// slot. When every slot is taken, a new kind replaces the kind that was least recently recorded.

#include "../src/fault_memory.hpp"
#include "unity.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <type_traits>

using namespace idfxx::persistent_log;

// The tests use a memory of eight kinds, whatever the suppressed-faults option is set to.
using memory = fault_memory<8>;

static_assert(std::is_trivially_default_constructible_v<memory>);
static_assert(std::is_trivially_copyable_v<memory>);
static_assert(std::is_standard_layout_v<memory>);
static_assert(std::has_unique_object_representations_v<memory>);
static_assert(memory::capacity == 8);
static_assert(fault_memory<1>::capacity == 1);
static_assert(sizeof(fault_memory<1>) == 3 * sizeof(uint32_t));

namespace {

// Stands in for the CRC of a kind. Any nonzero word will do.
constexpr uint32_t kind(uint32_t n) {
    return 0x4B000000 + n;
}

// Returns the memory as it is after a power-on that left every word zero.
template<size_t N = 8>
fault_memory<N> zeroed() {
    fault_memory<N> m;
    std::memset(&m, 0, sizeof m);
    return m;
}

// Remembers kinds 1 to 8, in that order.
memory full() {
    auto m = zeroed();
    for (uint32_t k = 1; k <= memory::capacity; ++k) {
        m.remember(kind(k));
    }
    return m;
}

template<size_t N>
void assert_holds_only(const fault_memory<N>& m, std::initializer_list<uint32_t> kinds) {
    for (uint32_t k = 1; k <= 12; ++k) {
        bool wanted = false;
        for (uint32_t w : kinds) {
            wanted = wanted || w == k;
        }
        char what[24];
        std::snprintf(what, sizeof what, "kind %u", static_cast<unsigned>(k));
        TEST_ASSERT_EQUAL_MESSAGE(wanted, m.holds(kind(k)), what);
    }
}

} // namespace

// =============================================================================

TEST_CASE("fault_memory zeroed holds nothing", "[persistent_log]") {
    auto m = zeroed();
    assert_holds_only(m, {});
    TEST_ASSERT_FALSE(m.repeats(kind(1)));
    TEST_ASSERT_FALSE(m.forget(kind(1)));
}

TEST_CASE("fault_memory takes a kind of 0 for an empty slot", "[persistent_log]") {
    // As documented, 0 marks an empty slot, so a kind whose CRC is 0 matches an empty slot.
    auto m = zeroed();
    TEST_ASSERT_TRUE(m.holds(0));
    TEST_ASSERT_TRUE(m.repeats(0));
    m = full();
    TEST_ASSERT_FALSE(m.holds(0));
    TEST_ASSERT_FALSE(m.repeats(0));
}

TEST_CASE("fault_memory tells a repeat from the first of a kind", "[persistent_log]") {
    auto m = zeroed();
    TEST_ASSERT_FALSE(m.repeats(kind(1)));
    m.remember(kind(1));
    TEST_ASSERT_TRUE(m.repeats(kind(1)));
    TEST_ASSERT_TRUE(m.holds(kind(1)));
    // Another kind is not a repeat.
    TEST_ASSERT_FALSE(m.repeats(kind(2)));
    assert_holds_only(m, {1});
}

TEST_CASE("fault_memory remembering a kind it holds keeps its slot", "[persistent_log]") {
    auto m = zeroed();
    m.remember(kind(1));
    m.remember(kind(2));
    m.remember(kind(1));
    TEST_ASSERT_TRUE(m.repeats(kind(1)));
    TEST_ASSERT_TRUE(m.repeats(kind(2)));
    // Kind 1 was remembered again in its own slot, so only two slots are taken.
    int taken = 0;
    for (const auto& r : m.slots) {
        taken += r.kind != 0 ? 1 : 0;
    }
    TEST_ASSERT_EQUAL_INT(2, taken);
}

TEST_CASE("fault_memory forgets a kind, and its next fault isn't a repeat", "[persistent_log]") {
    auto m = zeroed();
    m.remember(kind(1));
    m.remember(kind(2));
    TEST_ASSERT_TRUE(m.forget(kind(1)));
    TEST_ASSERT_FALSE(m.holds(kind(1)));
    TEST_ASSERT_FALSE(m.repeats(kind(1)));
    TEST_ASSERT_TRUE(m.repeats(kind(2)));
    TEST_ASSERT_FALSE(m.forget(kind(1)));
    m.remember(kind(1));
    TEST_ASSERT_TRUE(m.repeats(kind(1)));
    assert_holds_only(m, {1, 2});
}

TEST_CASE("fault_memory forgets a kind it doesn't hold as a no-op", "[persistent_log]") {
    auto m = zeroed();
    m.remember(kind(1));
    m.remember(kind(2));
    const memory before = m;
    TEST_ASSERT_FALSE(m.forget(kind(3)));
    TEST_ASSERT_EQUAL_MEMORY(&before, &m, sizeof m);
}

TEST_CASE("fault_memory full makes way for a new kind by the least recent", "[persistent_log]") {
    auto m = full();
    assert_holds_only(m, {1, 2, 3, 4, 5, 6, 7, 8});
    for (uint32_t k = 1; k <= 8; ++k) {
        TEST_ASSERT_TRUE(m.repeats(kind(k)));
    }
    m.remember(kind(9));
    assert_holds_only(m, {2, 3, 4, 5, 6, 7, 8, 9});
    TEST_ASSERT_FALSE(m.repeats(kind(1)));
    TEST_ASSERT_TRUE(m.repeats(kind(9)));
    // Next, kind 2 is replaced, because it is now the least recent.
    m.remember(kind(10));
    assert_holds_only(m, {3, 4, 5, 6, 7, 8, 9, 10});
    // When kind 1 is remembered again, it replaces kind 3.
    m.remember(kind(1));
    assert_holds_only(m, {1, 4, 5, 6, 7, 8, 9, 10});
}

TEST_CASE("fault_memory recording a kind again makes it the most recent", "[persistent_log]") {
    auto m = full();
    // Kind 1 is the least recent. Once it is recorded again, kind 2 is the least recent.
    m.remember(kind(1));
    m.remember(kind(9));
    assert_holds_only(m, {1, 3, 4, 5, 6, 7, 8, 9});
    TEST_ASSERT_TRUE(m.repeats(kind(1)));
    // Once kind 5 is recorded again, kinds 3, 4 and 6 are replaced before it.
    m.remember(kind(5));
    m.remember(kind(10));
    m.remember(kind(11));
    m.remember(kind(12));
    TEST_ASSERT_TRUE(m.holds(kind(5)));
    TEST_ASSERT_FALSE(m.holds(kind(3)));
    TEST_ASSERT_FALSE(m.holds(kind(4)));
    TEST_ASSERT_FALSE(m.holds(kind(6)));
    TEST_ASSERT_TRUE(m.holds(kind(7)));
}

TEST_CASE("fault_memory full puts a new kind in a forgotten one's slot", "[persistent_log]") {
    auto m = full();
    TEST_ASSERT_TRUE(m.forget(kind(5)));
    m.remember(kind(9));
    assert_holds_only(m, {1, 2, 3, 4, 6, 7, 8, 9});
    // When the memory is full again, kind 1, the least recent, is replaced.
    m.remember(kind(10));
    assert_holds_only(m, {2, 3, 4, 6, 7, 8, 9, 10});
}

TEST_CASE("fault_memory clear empties it and starts the order again", "[persistent_log]") {
    // Start from a memory as a power-on may leave it, with every bit set.
    memory m;
    std::memset(&m, 0xFF, sizeof m);
    m.clear();
    TEST_ASSERT_EQUAL_UINT32(0, m.next_order);
    for (const auto& r : m.slots) {
        TEST_ASSERT_EQUAL_UINT32(0, r.kind);
        TEST_ASSERT_EQUAL_UINT32(0, r.order);
    }
    assert_holds_only(m, {});

    m = full();
    m.remember(kind(9));
    m.clear();
    assert_holds_only(m, {});
    TEST_ASSERT_EQUAL_UINT32(0, m.next_order);
    TEST_ASSERT_FALSE(m.repeats(kind(2)));
    // When the memory is filled again, the first kind remembered after the clear is the first to be
    // replaced.
    for (uint32_t k = 4; k <= 11; ++k) {
        m.remember(kind(k));
    }
    m.remember(kind(1));
    assert_holds_only(m, {1, 5, 6, 7, 8, 9, 10, 11});
}

TEST_CASE("fault_memory with one slot holds the latest kind", "[persistent_log]") {
    auto m = zeroed<1>();
    assert_holds_only(m, {});
    m.remember(kind(1));
    TEST_ASSERT_TRUE(m.repeats(kind(1)));
    assert_holds_only(m, {1});
    // When kind 1 is remembered again, it keeps the slot.
    m.remember(kind(1));
    TEST_ASSERT_TRUE(m.repeats(kind(1)));
    assert_holds_only(m, {1});
    // A new kind replaces the only kind in the memory.
    m.remember(kind(2));
    assert_holds_only(m, {2});
    TEST_ASSERT_TRUE(m.repeats(kind(2)));
    TEST_ASSERT_FALSE(m.repeats(kind(1)));
    // Once kind 2 is forgotten, the slot is empty again, and the next kind takes it.
    TEST_ASSERT_FALSE(m.forget(kind(1)));
    TEST_ASSERT_TRUE(m.forget(kind(2)));
    assert_holds_only(m, {});
    TEST_ASSERT_EQUAL_UINT32(0, m.slots[0].kind);
    m.remember(kind(3));
    assert_holds_only(m, {3});
    TEST_ASSERT_EQUAL_UINT32(4, m.next_order);
    m.clear();
    assert_holds_only(m, {});
    TEST_ASSERT_EQUAL_UINT32(0, m.next_order);
}
