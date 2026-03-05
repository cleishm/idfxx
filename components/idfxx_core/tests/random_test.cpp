// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx random number generation
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/random.hpp"
#include "unity.h"

#include <random>
#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// random_device satisfies UniformRandomBitGenerator
static_assert(std::uniform_random_bit_generator<random_device>);

static_assert(std::is_same_v<random_device::result_type, uint32_t>);
static_assert(random_device::min() == 0);
static_assert(random_device::max() == UINT32_MAX);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("random() returns a value", "[idfxx][random]") {
    // Just verify it doesn't crash; any value is valid
    [[maybe_unused]] auto val = idfxx::random();
}

TEST_CASE("fill_random fills non-zero data", "[idfxx][random]") {
    std::array<uint8_t, 32> buf{};
    fill_random(std::span{buf});

    // It's astronomically unlikely that 32 random bytes are all zero
    bool all_zero = true;
    for (auto b : buf) {
        if (b != 0) {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zero);
}

TEST_CASE("fill_random with std::byte span", "[idfxx][random]") {
    std::array<std::byte, 16> buf{};
    fill_random(std::span{buf});

    bool all_zero = true;
    for (auto b : buf) {
        if (b != std::byte{0}) {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zero);
}

TEST_CASE("random_device with uniform_int_distribution", "[idfxx][random]") {
    random_device rng;
    std::uniform_int_distribution<int> dist(1, 100);

    int val = dist(rng);
    TEST_ASSERT_GREATER_OR_EQUAL(1, val);
    TEST_ASSERT_LESS_OR_EQUAL(100, val);
}
