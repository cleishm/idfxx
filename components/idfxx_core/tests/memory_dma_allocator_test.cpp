// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::dma_allocator
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/memory"
#include "unity.h"

#include <esp_memory_utils.h>
#include <type_traits>
#include <vector>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// value_type trait
static_assert(std::is_same_v<dma_allocator<int>::value_type, int>);
static_assert(std::is_same_v<dma_allocator<char>::value_type, char>);
static_assert(std::is_same_v<dma_allocator<double>::value_type, double>);

// Default constructible
static_assert(std::is_default_constructible_v<dma_allocator<int>>);
static_assert(std::is_default_constructible_v<dma_allocator<char>>);

// Nothrow default constructible
static_assert(std::is_nothrow_default_constructible_v<dma_allocator<int>>);

// Copy constructible from different type instantiation (rebind)
static_assert(std::is_constructible_v<dma_allocator<int>, const dma_allocator<char>&>);
static_assert(std::is_constructible_v<dma_allocator<double>, const dma_allocator<int>&>);

// Nothrow copy constructible from different type
static_assert(std::is_nothrow_constructible_v<dma_allocator<int>, const dma_allocator<char>&>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("dma_allocator basic allocation", "[idfxx][memory]") {
    dma_allocator<uint8_t> alloc;

    uint8_t* p = alloc.allocate(256);
    TEST_ASSERT_NOT_NULL(p);

    // Write to allocated memory to verify it's usable
    for (int i = 0; i < 256; ++i) {
        p[i] = static_cast<uint8_t>(i);
    }

    // Verify writes
    for (int i = 0; i < 256; ++i) {
        TEST_ASSERT_EQUAL(static_cast<uint8_t>(i), p[i]);
    }

    alloc.deallocate(p, 256);
}

TEST_CASE("dma_allocator memory is DMA-capable", "[idfxx][memory]") {
    dma_allocator<uint8_t> alloc;

    uint8_t* p = alloc.allocate(1);
    TEST_ASSERT_NOT_NULL(p);

    // DMA-capable memory must be in internal RAM
    TEST_ASSERT_TRUE(esp_ptr_internal(p));

    alloc.deallocate(p, 1);
}

TEST_CASE("dma_allocator with std::vector", "[idfxx][memory]") {
    std::vector<uint8_t, dma_allocator<uint8_t>> vec;

    vec.push_back(0xAA);
    vec.push_back(0xBB);
    vec.push_back(0xCC);

    TEST_ASSERT_EQUAL(3, vec.size());
    TEST_ASSERT_EQUAL(0xAA, vec[0]);
    TEST_ASSERT_EQUAL(0xBB, vec[1]);
    TEST_ASSERT_EQUAL(0xCC, vec[2]);

    // Verify vector's memory is DMA-capable (internal)
    if (!vec.empty()) {
        TEST_ASSERT_TRUE(esp_ptr_internal(vec.data()));
    }
}

TEST_CASE("dma_allocator equality", "[idfxx][memory]") {
    dma_allocator<int> alloc1;
    dma_allocator<int> alloc2;
    dma_allocator<char> alloc3;

    // All dma_allocators are stateless and compare equal
    TEST_ASSERT_TRUE(alloc1 == alloc2);
    TEST_ASSERT_TRUE(alloc1 == alloc3);
    TEST_ASSERT_FALSE(alloc1 != alloc2);
    TEST_ASSERT_FALSE(alloc1 != alloc3);
}

TEST_CASE("dma_allocator rebind construction", "[idfxx][memory]") {
    dma_allocator<char> char_alloc;
    dma_allocator<int> int_alloc(char_alloc);

    // Both should work independently
    char* cp = char_alloc.allocate(10);
    int* ip = int_alloc.allocate(10);

    TEST_ASSERT_NOT_NULL(cp);
    TEST_ASSERT_NOT_NULL(ip);

    char_alloc.deallocate(cp, 10);
    int_alloc.deallocate(ip, 10);
}
