// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx allocation functions
// Uses ESP-IDF Unity test framework

#include "idfxx/memory"
#include "unity.h"

#include <cstdint>
#include <cstring>

using enum idfxx::memory::caps;

// =============================================================================
// idfxx::malloc / idfxx::free
// =============================================================================

TEST_CASE("idfxx::malloc allocates writable memory", "[idfxx][memory]") {
    void* p = idfxx::malloc(64, default_heap);
    TEST_ASSERT_NOT_NULL(p);
    std::memset(p, 0xAB, 64);
    TEST_ASSERT_EQUAL_UINT8(0xAB, static_cast<uint8_t*>(p)[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, static_cast<uint8_t*>(p)[63]);
    idfxx::free(p);
}

TEST_CASE("idfxx::malloc returns nullptr for excessive size", "[idfxx][memory]") {
    void* p = idfxx::malloc(SIZE_MAX, default_heap);
    TEST_ASSERT_NULL(p);
}

// =============================================================================
// idfxx::calloc
// =============================================================================

TEST_CASE("idfxx::calloc allocates zero-initialized memory", "[idfxx][memory]") {
    void* p = idfxx::calloc(16, sizeof(uint32_t), default_heap);
    TEST_ASSERT_NOT_NULL(p);
    auto* values = static_cast<uint32_t*>(p);
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT32(0, values[i]);
    }
    idfxx::free(p);
}

// =============================================================================
// idfxx::realloc
// =============================================================================

TEST_CASE("idfxx::realloc grows allocation preserving data", "[idfxx][memory]") {
    void* p = idfxx::malloc(32, default_heap);
    TEST_ASSERT_NOT_NULL(p);
    std::memset(p, 0xCD, 32);

    void* p2 = idfxx::realloc(p, 64, default_heap);
    TEST_ASSERT_NOT_NULL(p2);
    auto* bytes = static_cast<uint8_t*>(p2);
    for (int i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xCD, bytes[i]);
    }
    idfxx::free(p2);
}

TEST_CASE("idfxx::realloc with nullptr acts like malloc", "[idfxx][memory]") {
    void* p = idfxx::realloc(nullptr, 64, default_heap);
    TEST_ASSERT_NOT_NULL(p);
    idfxx::free(p);
}

// =============================================================================
// idfxx::free
// =============================================================================

TEST_CASE("idfxx::free with nullptr is safe", "[idfxx][memory]") {
    idfxx::free(nullptr); // must not crash
}

// =============================================================================
// idfxx::aligned_alloc
// =============================================================================

TEST_CASE("idfxx::aligned_alloc returns aligned pointer", "[idfxx][memory]") {
    constexpr size_t alignment = 64;
    void* p = idfxx::aligned_alloc(alignment, 128, default_heap);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT(0, reinterpret_cast<uintptr_t>(p) % alignment);
    idfxx::free(p);
}

// =============================================================================
// idfxx::aligned_calloc
// =============================================================================

TEST_CASE("idfxx::aligned_calloc returns aligned zero-initialized memory", "[idfxx][memory]") {
    constexpr size_t alignment = 64;
    void* p = idfxx::aligned_calloc(alignment, 16, sizeof(uint32_t), default_heap);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT(0, reinterpret_cast<uintptr_t>(p) % alignment);
    auto* values = static_cast<uint32_t*>(p);
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT32(0, values[i]);
    }
    idfxx::free(p);
}
