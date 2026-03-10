// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx interrupt allocation types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/intr_alloc.hpp"
#include "unity.h"

#include <esp_intr_alloc.h>
#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// --- intr_level ---

// intr_level is an enum class with int as underlying type
static_assert(std::is_enum_v<intr_level>);
static_assert(std::is_same_v<std::underlying_type_t<intr_level>, int>);

// intr_level has enable_flags_operators enabled
static_assert(enable_flags_operators<intr_level>);
static_assert(flag_enum<intr_level>);

// intr_level values match ESP-IDF constants
static_assert(std::to_underlying(intr_level::level_1) == ESP_INTR_FLAG_LEVEL1);
static_assert(std::to_underlying(intr_level::level_2) == ESP_INTR_FLAG_LEVEL2);
static_assert(std::to_underlying(intr_level::level_3) == ESP_INTR_FLAG_LEVEL3);
static_assert(std::to_underlying(intr_level::level_4) == ESP_INTR_FLAG_LEVEL4);
static_assert(std::to_underlying(intr_level::level_5) == ESP_INTR_FLAG_LEVEL5);
static_assert(std::to_underlying(intr_level::level_6) == ESP_INTR_FLAG_LEVEL6);
static_assert(std::to_underlying(intr_level::nmi) == ESP_INTR_FLAG_NMI);

// Predefined masks have correct values
static_assert(to_underlying(intr_level_lowmed) ==
              (std::to_underlying(intr_level::level_1) | std::to_underlying(intr_level::level_2) |
               std::to_underlying(intr_level::level_3)));

static_assert(to_underlying(intr_level_high) ==
              (std::to_underlying(intr_level::level_4) | std::to_underlying(intr_level::level_5) |
               std::to_underlying(intr_level::level_6) | std::to_underlying(intr_level::nmi)));

static_assert(to_underlying(intr_level_all) == (to_underlying(intr_level_lowmed) | to_underlying(intr_level_high)));

// Levels can be combined at compile time
static_assert((intr_level::level_1 | intr_level::level_2).contains(intr_level::level_1));
static_assert((intr_level::level_1 | intr_level::level_2).contains(intr_level::level_2));

// --- intr_flag ---

// intr_flag is an enum class with int as underlying type
static_assert(std::is_enum_v<intr_flag>);
static_assert(std::is_same_v<std::underlying_type_t<intr_flag>, int>);

// intr_flag has enable_flags_operators enabled
static_assert(enable_flags_operators<intr_flag>);
static_assert(flag_enum<intr_flag>);

// intr_flag values match ESP-IDF constants
static_assert(std::to_underlying(intr_flag::none) == 0);
static_assert(std::to_underlying(intr_flag::shared) == ESP_INTR_FLAG_SHARED);
static_assert(std::to_underlying(intr_flag::edge) == ESP_INTR_FLAG_EDGE);
static_assert(std::to_underlying(intr_flag::iram) == ESP_INTR_FLAG_IRAM);
static_assert(std::to_underlying(intr_flag::disabled) == ESP_INTR_FLAG_INTRDISABLED);

// Flags can be combined at compile time
static_assert((intr_flag::shared | intr_flag::edge).contains(intr_flag::shared));
static_assert((intr_flag::shared | intr_flag::edge).contains(intr_flag::edge));
static_assert(to_underlying(intr_flag::shared | intr_flag::edge) ==
              (std::to_underlying(intr_flag::shared) | std::to_underlying(intr_flag::edge)));

// --- Bit ranges don't overlap ---
static_assert((to_underlying(intr_level_all) & to_underlying(intr_flag::shared | intr_flag::edge | intr_flag::iram |
                                                              intr_flag::disabled)) == 0);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// --- intr_level runtime tests ---

TEST_CASE("intr_level default is empty", "[idfxx][hw_support][intr_alloc]") {
    intr_levels f;

    TEST_ASSERT_TRUE(f.empty());
    TEST_ASSERT_EQUAL_INT(0, to_underlying(f));
}

TEST_CASE("intr_level single value construction", "[idfxx][hw_support][intr_alloc]") {
    intr_levels f{intr_level::level_1};

    TEST_ASSERT_FALSE(f.empty());
    TEST_ASSERT_EQUAL_INT(ESP_INTR_FLAG_LEVEL1, to_underlying(f));
}

TEST_CASE("intr_level combine with |", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_level::level_1 | intr_level::level_2;

    TEST_ASSERT_TRUE(f.contains(intr_level::level_1));
    TEST_ASSERT_TRUE(f.contains(intr_level::level_2));
    TEST_ASSERT_FALSE(f.contains(intr_level::level_3));
}

TEST_CASE("intr_level_lowmed contains levels 1-3", "[idfxx][hw_support][intr_alloc]") {
    TEST_ASSERT_TRUE(intr_level_lowmed.contains(intr_level::level_1));
    TEST_ASSERT_TRUE(intr_level_lowmed.contains(intr_level::level_2));
    TEST_ASSERT_TRUE(intr_level_lowmed.contains(intr_level::level_3));
    TEST_ASSERT_FALSE(intr_level_lowmed.contains(intr_level::level_4));
    TEST_ASSERT_FALSE(intr_level_lowmed.contains(intr_level::level_5));
    TEST_ASSERT_FALSE(intr_level_lowmed.contains(intr_level::level_6));
    TEST_ASSERT_FALSE(intr_level_lowmed.contains(intr_level::nmi));
}

TEST_CASE("intr_level_high contains levels 4-6 and NMI", "[idfxx][hw_support][intr_alloc]") {
    TEST_ASSERT_FALSE(intr_level_high.contains(intr_level::level_1));
    TEST_ASSERT_FALSE(intr_level_high.contains(intr_level::level_2));
    TEST_ASSERT_FALSE(intr_level_high.contains(intr_level::level_3));
    TEST_ASSERT_TRUE(intr_level_high.contains(intr_level::level_4));
    TEST_ASSERT_TRUE(intr_level_high.contains(intr_level::level_5));
    TEST_ASSERT_TRUE(intr_level_high.contains(intr_level::level_6));
    TEST_ASSERT_TRUE(intr_level_high.contains(intr_level::nmi));
}

TEST_CASE("intr_level_all contains all levels", "[idfxx][hw_support][intr_alloc]") {
    TEST_ASSERT_TRUE(intr_level_all.contains(intr_level::level_1));
    TEST_ASSERT_TRUE(intr_level_all.contains(intr_level::level_2));
    TEST_ASSERT_TRUE(intr_level_all.contains(intr_level::level_3));
    TEST_ASSERT_TRUE(intr_level_all.contains(intr_level::level_4));
    TEST_ASSERT_TRUE(intr_level_all.contains(intr_level::level_5));
    TEST_ASSERT_TRUE(intr_level_all.contains(intr_level::level_6));
    TEST_ASSERT_TRUE(intr_level_all.contains(intr_level::nmi));
}

// --- intr_flag runtime tests ---

TEST_CASE("intr_flag default is none", "[idfxx][hw_support][intr_alloc]") {
    flags<intr_flag> f;

    TEST_ASSERT_TRUE(f.empty());
    TEST_ASSERT_EQUAL_INT(0, to_underlying(f));
}

TEST_CASE("intr_flag shared and edge flags", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag::shared | intr_flag::edge;

    TEST_ASSERT_TRUE(f.contains(intr_flag::shared));
    TEST_ASSERT_TRUE(f.contains(intr_flag::edge));
    TEST_ASSERT_FALSE(f.contains(intr_flag::iram));
}

TEST_CASE("intr_flag iram flag", "[idfxx][hw_support][intr_alloc]") {
    flags<intr_flag> f{intr_flag::iram};

    TEST_ASSERT_TRUE(f.contains(intr_flag::iram));
    TEST_ASSERT_FALSE(f.contains(intr_flag::shared));
}

TEST_CASE("intr_flag equality comparison", "[idfxx][hw_support][intr_alloc]") {
    auto f1 = intr_flag::shared | intr_flag::iram;
    auto f2 = intr_flag::shared | intr_flag::iram;
    auto f3 = intr_flag::edge | intr_flag::iram;

    TEST_ASSERT_TRUE(f1 == f2);
    TEST_ASSERT_FALSE(f1 == f3);
}

// --- Combining levels and flags for ESP-IDF ---

TEST_CASE("intr_level and intr_flag can be combined for ESP-IDF", "[idfxx][hw_support][intr_alloc]") {
    auto levels = intr_level::level_1 | intr_level::level_2;
    flags<intr_flag> f{intr_flag::iram};

    int combined = static_cast<int>(to_underlying(levels)) | static_cast<int>(to_underlying(f));

    TEST_ASSERT_EQUAL_INT(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM, combined);
}
