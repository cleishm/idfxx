// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx heap query functions
// Uses ESP-IDF Unity test framework

#include "idfxx/memory"
#include "unity.h"

using namespace idfxx;

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("heap_total_size returns > 0 for default_heap", "[idfxx][memory][heap]") {
    size_t total = heap_total_size(memory_caps::default_heap);
    TEST_ASSERT_GREATER_THAN(0, total);
}

TEST_CASE("heap_free_size returns > 0 for default_heap", "[idfxx][memory][heap]") {
    size_t free_sz = heap_free_size(memory_caps::default_heap);
    TEST_ASSERT_GREATER_THAN(0, free_sz);
}

TEST_CASE("heap_free_size <= heap_total_size", "[idfxx][memory][heap]") {
    auto caps = memory_caps::default_heap;
    size_t free_sz = heap_free_size(caps);
    size_t total = heap_total_size(caps);
    TEST_ASSERT_LESS_OR_EQUAL(total, free_sz);
}

TEST_CASE("heap_largest_free_block <= heap_free_size", "[idfxx][memory][heap]") {
    auto caps = memory_caps::default_heap;
    size_t largest = heap_largest_free_block(caps);
    size_t free_sz = heap_free_size(caps);
    TEST_ASSERT_LESS_OR_EQUAL(free_sz, largest);
}

TEST_CASE("heap_minimum_free_size <= heap_total_size", "[idfxx][memory][heap]") {
    auto caps = memory_caps::default_heap;
    size_t min_free = heap_minimum_free_size(caps);
    size_t total = heap_total_size(caps);
    TEST_ASSERT_LESS_OR_EQUAL(total, min_free);
}

TEST_CASE("get_heap_info returns consistent values", "[idfxx][memory][heap]") {
    auto info = get_heap_info(memory_caps::default_heap);

    TEST_ASSERT_GREATER_THAN(0, info.total_free_bytes);
    TEST_ASSERT_GREATER_THAN(0, info.total_allocated_bytes);
    TEST_ASSERT_GREATER_THAN(0, info.largest_free_block);
    TEST_ASSERT_LESS_OR_EQUAL(info.total_free_bytes, info.largest_free_block);
    TEST_ASSERT_LESS_OR_EQUAL(info.total_free_bytes + info.total_allocated_bytes, info.minimum_free_bytes);
    TEST_ASSERT_EQUAL(info.allocated_blocks + info.free_blocks, info.total_blocks);
}

// =============================================================================
// Heap walking tests
// =============================================================================

TEST_CASE("heap_walk invokes callback with valid blocks", "[idfxx][memory][heap]") {
    int count = 0;
    heap_walk(memory_caps::default_heap, [&](heap_region region, heap_block block) {
        TEST_ASSERT_GREATER_THAN(0, region.end - region.start);
        TEST_ASSERT_GREATER_THAN(0, block.size);
        ++count;
        return true;
    });
    TEST_ASSERT_GREATER_THAN(0, count);
}

TEST_CASE("heap_walk_all invokes callback", "[idfxx][memory][heap]") {
    int count = 0;
    heap_walk_all([&](heap_region, heap_block) {
        ++count;
        return true;
    });
    TEST_ASSERT_GREATER_THAN(0, count);
}

TEST_CASE("heap_walk early termination", "[idfxx][memory][heap]") {
    int count = 0;
    heap_walk(memory_caps::default_heap, [&](heap_region, heap_block) {
        ++count;
        return false; // stop after first block
    });
    // Should have visited at least one block but stopped early
    TEST_ASSERT_GREATER_THAN(0, count);
}

// =============================================================================
// Heap integrity tests
// =============================================================================

TEST_CASE("heap_check_integrity returns true for default_heap", "[idfxx][memory][heap]") {
    TEST_ASSERT_TRUE(heap_check_integrity(memory_caps::default_heap));
}

TEST_CASE("heap_check_integrity_all returns true", "[idfxx][memory][heap]") {
    TEST_ASSERT_TRUE(heap_check_integrity_all());
}
