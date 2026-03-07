// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx heap allocation functions
// Uses ESP-IDF Unity test framework

#include "idfxx/memory"
#include "unity.h"

#include <cstdint>
#include <cstring>

using namespace idfxx;

// =============================================================================
// heap_malloc / heap_free
// =============================================================================

TEST_CASE("heap_malloc allocates writable memory", "[idfxx][memory][heap]") {
    void* p = heap_malloc(64, memory_caps::default_heap);
    TEST_ASSERT_NOT_NULL(p);
    std::memset(p, 0xAB, 64);
    TEST_ASSERT_EQUAL_UINT8(0xAB, static_cast<uint8_t*>(p)[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, static_cast<uint8_t*>(p)[63]);
    heap_free(p);
}

TEST_CASE("heap_malloc returns nullptr for excessive size", "[idfxx][memory][heap]") {
    void* p = heap_malloc(SIZE_MAX, memory_caps::default_heap);
    TEST_ASSERT_NULL(p);
}

// =============================================================================
// heap_calloc
// =============================================================================

TEST_CASE("heap_calloc allocates zero-initialized memory", "[idfxx][memory][heap]") {
    void* p = heap_calloc(16, sizeof(uint32_t), memory_caps::default_heap);
    TEST_ASSERT_NOT_NULL(p);
    auto* values = static_cast<uint32_t*>(p);
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT32(0, values[i]);
    }
    heap_free(p);
}

// =============================================================================
// heap_realloc
// =============================================================================

TEST_CASE("heap_realloc grows allocation preserving data", "[idfxx][memory][heap]") {
    void* p = heap_malloc(32, memory_caps::default_heap);
    TEST_ASSERT_NOT_NULL(p);
    std::memset(p, 0xCD, 32);

    void* p2 = heap_realloc(p, 64, memory_caps::default_heap);
    TEST_ASSERT_NOT_NULL(p2);
    auto* bytes = static_cast<uint8_t*>(p2);
    for (int i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xCD, bytes[i]);
    }
    heap_free(p2);
}

TEST_CASE("heap_realloc with nullptr acts like malloc", "[idfxx][memory][heap]") {
    void* p = heap_realloc(nullptr, 64, memory_caps::default_heap);
    TEST_ASSERT_NOT_NULL(p);
    heap_free(p);
}

// =============================================================================
// heap_free
// =============================================================================

TEST_CASE("heap_free with nullptr is safe", "[idfxx][memory][heap]") {
    heap_free(nullptr); // must not crash
}

// =============================================================================
// heap_aligned_alloc
// =============================================================================

TEST_CASE("heap_aligned_alloc returns aligned pointer", "[idfxx][memory][heap]") {
    constexpr size_t alignment = 64;
    void* p = heap_aligned_alloc(alignment, 128, memory_caps::default_heap);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT(0, reinterpret_cast<uintptr_t>(p) % alignment);
    heap_free(p);
}

// =============================================================================
// heap_aligned_calloc
// =============================================================================

TEST_CASE("heap_aligned_calloc returns aligned zero-initialized memory", "[idfxx][memory][heap]") {
    constexpr size_t alignment = 64;
    void* p = heap_aligned_calloc(alignment, 16, sizeof(uint32_t), memory_caps::default_heap);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT(0, reinterpret_cast<uintptr_t>(p) % alignment);
    auto* values = static_cast<uint32_t*>(p);
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT32(0, values[i]);
    }
    heap_free(p);
}
