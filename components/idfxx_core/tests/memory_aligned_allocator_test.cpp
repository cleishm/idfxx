// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::aligned_dram_allocator
// Uses ESP-IDF Unity test framework

#include "idfxx/memory"
#include "unity.h"

#include <cstdint>
#include <esp_memory_utils.h>
#include <type_traits>
#include <vector>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// value_type trait
static_assert(std::is_same_v<aligned_dram_allocator<int, 16>::value_type, int>);
static_assert(std::is_same_v<aligned_dram_allocator<char, 32>::value_type, char>);

// Default constructible
static_assert(std::is_default_constructible_v<aligned_dram_allocator<int, 16>>);

// Nothrow default constructible
static_assert(std::is_nothrow_default_constructible_v<aligned_dram_allocator<int, 16>>);

// Copy constructible from different type instantiation (rebind)
static_assert(std::is_constructible_v<aligned_dram_allocator<int, 16>, const aligned_dram_allocator<char, 16>&>);

// Nothrow copy constructible from different type
static_assert(std::is_nothrow_constructible_v<aligned_dram_allocator<int, 16>, const aligned_dram_allocator<char, 16>&>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("aligned_dram_allocator<16> basic allocation and alignment", "[idfxx][memory]") {
    aligned_dram_allocator<int, 16> alloc;

    int* p = alloc.allocate(10);
    TEST_ASSERT_NOT_NULL(p);

    // Verify alignment
    TEST_ASSERT_EQUAL(0, reinterpret_cast<uintptr_t>(p) % 16);

    // Write and verify
    for (int i = 0; i < 10; ++i) {
        p[i] = i * 100;
    }
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL(i * 100, p[i]);
    }

    alloc.deallocate(p, 10);
}

TEST_CASE("aligned_dram_allocator<32> alignment", "[idfxx][memory]") {
    aligned_dram_allocator<uint8_t, 32> alloc;

    uint8_t* p = alloc.allocate(64);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(0, reinterpret_cast<uintptr_t>(p) % 32);

    alloc.deallocate(p, 64);
}

TEST_CASE("aligned_dram_allocator<128> alignment", "[idfxx][memory]") {
    aligned_dram_allocator<uint8_t, 128> alloc;

    uint8_t* p = alloc.allocate(256);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(0, reinterpret_cast<uintptr_t>(p) % 128);

    alloc.deallocate(p, 256);
}

TEST_CASE("aligned_dram_allocator memory is in internal DRAM", "[idfxx][memory]") {
    aligned_dram_allocator<int, 16> alloc;

    int* p = alloc.allocate(1);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(esp_ptr_internal(p));

    alloc.deallocate(p, 1);
}

TEST_CASE("aligned_dram_allocator with std::vector", "[idfxx][memory]") {
    std::vector<int, aligned_dram_allocator<int, 16>> vec;

    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    TEST_ASSERT_EQUAL(3, vec.size());
    TEST_ASSERT_EQUAL(1, vec[0]);
    TEST_ASSERT_EQUAL(2, vec[1]);
    TEST_ASSERT_EQUAL(3, vec[2]);

    if (!vec.empty()) {
        TEST_ASSERT_EQUAL(0, reinterpret_cast<uintptr_t>(vec.data()) % 16);
        TEST_ASSERT_TRUE(esp_ptr_internal(vec.data()));
    }
}

TEST_CASE("aligned_dram_allocator equality", "[idfxx][memory]") {
    aligned_dram_allocator<int, 16> alloc1;
    aligned_dram_allocator<int, 16> alloc2;
    aligned_dram_allocator<char, 16> alloc3;

    TEST_ASSERT_TRUE(alloc1 == alloc2);
    TEST_ASSERT_TRUE(alloc1 == alloc3);
    TEST_ASSERT_FALSE(alloc1 != alloc2);
    TEST_ASSERT_FALSE(alloc1 != alloc3);
}

TEST_CASE("aligned_dram_allocator rebind construction", "[idfxx][memory]") {
    aligned_dram_allocator<char, 16> char_alloc;
    aligned_dram_allocator<int, 16> int_alloc(char_alloc);

    char* cp = char_alloc.allocate(10);
    int* ip = int_alloc.allocate(10);

    TEST_ASSERT_NOT_NULL(cp);
    TEST_ASSERT_NOT_NULL(ip);

    char_alloc.deallocate(cp, 10);
    int_alloc.deallocate(ip, 10);
}
