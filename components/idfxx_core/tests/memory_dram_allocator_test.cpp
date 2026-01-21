// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx::dram_allocator
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/memory"
#include "unity.h"

#include <esp_memory_utils.h>
#include <string>
#include <type_traits>
#include <vector>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// value_type trait
static_assert(std::is_same_v<dram_allocator<int>::value_type, int>);
static_assert(std::is_same_v<dram_allocator<char>::value_type, char>);
static_assert(std::is_same_v<dram_allocator<double>::value_type, double>);

// Default constructible
static_assert(std::is_default_constructible_v<dram_allocator<int>>);
static_assert(std::is_default_constructible_v<dram_allocator<char>>);

// Nothrow default constructible
static_assert(std::is_nothrow_default_constructible_v<dram_allocator<int>>);

// Copy constructible from different type instantiation (rebind)
static_assert(std::is_constructible_v<dram_allocator<int>, const dram_allocator<char>&>);
static_assert(std::is_constructible_v<dram_allocator<double>, const dram_allocator<int>&>);

// Nothrow copy constructible from different type
static_assert(std::is_nothrow_constructible_v<dram_allocator<int>, const dram_allocator<char>&>);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("dram_allocator basic allocation", "[idfxx][memory]") {
    dram_allocator<int> alloc;

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

TEST_CASE("dram_allocator memory is in internal DRAM", "[idfxx][memory]") {
    dram_allocator<int> alloc;

    int* p = alloc.allocate(1);
    TEST_ASSERT_NOT_NULL(p);

    // Verify memory is in internal RAM
    TEST_ASSERT_TRUE(esp_ptr_internal(p));

    alloc.deallocate(p, 1);
}

TEST_CASE("dram_allocator with std::vector", "[idfxx][memory]") {
    std::vector<int, dram_allocator<int>> vec;

    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    TEST_ASSERT_EQUAL(3, vec.size());
    TEST_ASSERT_EQUAL(1, vec[0]);
    TEST_ASSERT_EQUAL(2, vec[1]);
    TEST_ASSERT_EQUAL(3, vec[2]);

    // Verify vector's memory is in internal DRAM
    if (!vec.empty()) {
        TEST_ASSERT_TRUE(esp_ptr_internal(vec.data()));
    }
}

TEST_CASE("dram_allocator with std::basic_string", "[idfxx][memory]") {
    using dram_string = std::basic_string<char, std::char_traits<char>, dram_allocator<char>>;

    dram_string str = "Hello, DRAM!";

    TEST_ASSERT_EQUAL(12, str.size());
    TEST_ASSERT_EQUAL_STRING("Hello, DRAM!", str.c_str());

    str += " More text.";
    TEST_ASSERT_EQUAL_STRING("Hello, DRAM! More text.", str.c_str());
}

TEST_CASE("dram_allocator equality", "[idfxx][memory]") {
    dram_allocator<int> alloc1;
    dram_allocator<int> alloc2;
    dram_allocator<char> alloc3;

    // All dram_allocators are stateless and compare equal
    TEST_ASSERT_TRUE(alloc1 == alloc2);
    TEST_ASSERT_TRUE(alloc1 == alloc3);
    TEST_ASSERT_FALSE(alloc1 != alloc2);
    TEST_ASSERT_FALSE(alloc1 != alloc3);
}

TEST_CASE("dram_allocator rebind construction", "[idfxx][memory]") {
    dram_allocator<char> char_alloc;
    dram_allocator<int> int_alloc(char_alloc);

    // Both should work independently
    char* cp = char_alloc.allocate(10);
    int* ip = int_alloc.allocate(10);

    TEST_ASSERT_NOT_NULL(cp);
    TEST_ASSERT_NOT_NULL(ip);

    char_alloc.deallocate(cp, 10);
    int_alloc.deallocate(ip, 10);
}
