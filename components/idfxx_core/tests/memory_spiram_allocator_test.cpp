// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::spiram_allocator
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/memory"
#include "unity.h"

#include <type_traits>
#include <vector>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// value_type trait
static_assert(std::is_same_v<spiram_allocator<int>::value_type, int>);
static_assert(std::is_same_v<spiram_allocator<char>::value_type, char>);
static_assert(std::is_same_v<spiram_allocator<double>::value_type, double>);

// Default constructible
static_assert(std::is_default_constructible_v<spiram_allocator<int>>);
static_assert(std::is_default_constructible_v<spiram_allocator<char>>);

// Nothrow default constructible
static_assert(std::is_nothrow_default_constructible_v<spiram_allocator<int>>);

// Copy constructible from different type instantiation (rebind)
static_assert(std::is_constructible_v<spiram_allocator<int>, const spiram_allocator<char>&>);
static_assert(std::is_constructible_v<spiram_allocator<double>, const spiram_allocator<int>&>);

// Nothrow copy constructible from different type
static_assert(std::is_nothrow_constructible_v<spiram_allocator<int>, const spiram_allocator<char>&>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

#if CONFIG_SPIRAM

TEST_CASE("spiram_allocator basic allocation", "[idfxx][memory]") {
    spiram_allocator<int> alloc;

    int* p = alloc.allocate(10);
    TEST_ASSERT_NOT_NULL(p);

    // Write to allocated memory to verify it's usable
    for (int i = 0; i < 10; ++i) {
        p[i] = i * 100;
    }

    // Verify writes
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL(i * 100, p[i]);
    }

    alloc.deallocate(p, 10);
}

TEST_CASE("spiram_allocator memory is in external PSRAM", "[idfxx][memory]") {
    spiram_allocator<int> alloc;

    int* p = alloc.allocate(1);
    TEST_ASSERT_NOT_NULL(p);

    // Verify memory is in external RAM
    TEST_ASSERT_TRUE(esp_ptr_external_ram(p));

    alloc.deallocate(p, 1);
}

TEST_CASE("spiram_allocator with std::vector", "[idfxx][memory]") {
    std::vector<uint8_t, spiram_allocator<uint8_t>> vec;

    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    TEST_ASSERT_EQUAL(3, vec.size());
    TEST_ASSERT_EQUAL(1, vec[0]);
    TEST_ASSERT_EQUAL(2, vec[1]);
    TEST_ASSERT_EQUAL(3, vec[2]);

    // Verify vector's memory is in external PSRAM
    if (!vec.empty()) {
        TEST_ASSERT_TRUE(esp_ptr_external_ram(vec.data()));
    }
}

#endif // CONFIG_SPIRAM

TEST_CASE("spiram_allocator equality", "[idfxx][memory]") {
    spiram_allocator<int> alloc1;
    spiram_allocator<int> alloc2;
    spiram_allocator<char> alloc3;

    // All spiram_allocators are stateless and compare equal
    TEST_ASSERT_TRUE(alloc1 == alloc2);
    TEST_ASSERT_TRUE(alloc1 == alloc3);
    TEST_ASSERT_FALSE(alloc1 != alloc2);
    TEST_ASSERT_FALSE(alloc1 != alloc3);
}

TEST_CASE("spiram_allocator rebind construction", "[idfxx][memory]") {
    spiram_allocator<char> char_alloc;
    spiram_allocator<int> int_alloc(char_alloc);

    // Both should compile and be usable (actual allocation only tested when SPIRAM available)
    (void)char_alloc;
    (void)int_alloc;
}
