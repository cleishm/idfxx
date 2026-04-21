// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::memory query functions
// Uses ESP-IDF Unity test framework

#include "idfxx/memory"
#include "unity.h"

namespace memory = idfxx::memory;
using enum memory::capabilities;

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("memory::total_size returns > 0 for default_heap", "[idfxx][memory]") {
    size_t total = memory::total_size(default_heap);
    TEST_ASSERT_GREATER_THAN(0, total);
}

TEST_CASE("memory::free_size returns > 0 for default_heap", "[idfxx][memory]") {
    size_t free_sz = memory::free_size(default_heap);
    TEST_ASSERT_GREATER_THAN(0, free_sz);
}

TEST_CASE("memory::free_size <= memory::total_size", "[idfxx][memory]") {
    size_t free_sz = memory::free_size(default_heap);
    size_t total = memory::total_size(default_heap);
    TEST_ASSERT_LESS_OR_EQUAL(total, free_sz);
}

TEST_CASE("memory::largest_free_block <= memory::free_size", "[idfxx][memory]") {
    size_t largest = memory::largest_free_block(default_heap);
    size_t free_sz = memory::free_size(default_heap);
    TEST_ASSERT_LESS_OR_EQUAL(free_sz, largest);
}

TEST_CASE("memory::minimum_free_size <= memory::total_size", "[idfxx][memory]") {
    size_t min_free = memory::minimum_free_size(default_heap);
    size_t total = memory::total_size(default_heap);
    TEST_ASSERT_LESS_OR_EQUAL(total, min_free);
}

TEST_CASE("memory::get_info returns consistent values", "[idfxx][memory]") {
    auto hi = memory::get_info(default_heap);

    TEST_ASSERT_GREATER_THAN(0, hi.total_free_bytes);
    TEST_ASSERT_GREATER_THAN(0, hi.total_allocated_bytes);
    TEST_ASSERT_GREATER_THAN(0, hi.largest_free_block);
    TEST_ASSERT_LESS_OR_EQUAL(hi.total_free_bytes, hi.largest_free_block);
    TEST_ASSERT_LESS_OR_EQUAL(hi.total_free_bytes + hi.total_allocated_bytes, hi.minimum_free_bytes);
    TEST_ASSERT_EQUAL(hi.allocated_blocks + hi.free_blocks, hi.total_blocks);
}

// =============================================================================
// Heap walking tests
// =============================================================================

TEST_CASE("memory::walk invokes callback with valid blocks", "[idfxx][memory]") {
    int count = 0;
    memory::walk(default_heap, [&](memory::region rgn, memory::block blk) {
        TEST_ASSERT_GREATER_THAN(0, rgn.end - rgn.start);
        TEST_ASSERT_GREATER_THAN(0, blk.size);
        ++count;
        return true;
    });
    TEST_ASSERT_GREATER_THAN(0, count);
}

TEST_CASE("memory::walk (all heaps) invokes callback", "[idfxx][memory]") {
    int count = 0;
    memory::walk([&](memory::region, memory::block) {
        ++count;
        return true;
    });
    TEST_ASSERT_GREATER_THAN(0, count);
}

TEST_CASE("memory::walk early termination", "[idfxx][memory]") {
    int count = 0;
    memory::walk(default_heap, [&](memory::region, memory::block) {
        ++count;
        return false; // stop after first block
    });
    // Should have visited at least one block but stopped early
    TEST_ASSERT_GREATER_THAN(0, count);
}

// =============================================================================
// Heap integrity tests
// =============================================================================

TEST_CASE("memory::check_integrity returns true for default_heap", "[idfxx][memory]") {
    TEST_ASSERT_TRUE(memory::check_integrity(default_heap));
}

TEST_CASE("memory::check_integrity (all heaps) returns true", "[idfxx][memory]") {
    TEST_ASSERT_TRUE(memory::check_integrity());
}
