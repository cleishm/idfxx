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

// intr_flag is an enum class with int as underlying type
static_assert(std::is_enum_v<intr_flag>);
static_assert(std::is_same_v<std::underlying_type_t<intr_flag>, int>);

// intr_flag has enable_flags_operators enabled
static_assert(enable_flags_operators<intr_flag>);
static_assert(flag_enum<intr_flag>);

// intr_flag values match ESP-IDF constants
static_assert(std::to_underlying(intr_flag::none) == 0);
static_assert(std::to_underlying(intr_flag::level1) == ESP_INTR_FLAG_LEVEL1);
static_assert(std::to_underlying(intr_flag::level2) == ESP_INTR_FLAG_LEVEL2);
static_assert(std::to_underlying(intr_flag::level3) == ESP_INTR_FLAG_LEVEL3);
static_assert(std::to_underlying(intr_flag::level4) == ESP_INTR_FLAG_LEVEL4);
static_assert(std::to_underlying(intr_flag::level5) == ESP_INTR_FLAG_LEVEL5);
static_assert(std::to_underlying(intr_flag::level6) == ESP_INTR_FLAG_LEVEL6);
static_assert(std::to_underlying(intr_flag::nmi) == ESP_INTR_FLAG_NMI);
static_assert(std::to_underlying(intr_flag::shared) == ESP_INTR_FLAG_SHARED);
static_assert(std::to_underlying(intr_flag::edge) == ESP_INTR_FLAG_EDGE);
static_assert(std::to_underlying(intr_flag::iram) == ESP_INTR_FLAG_IRAM);
static_assert(std::to_underlying(intr_flag::intr_disabled) == ESP_INTR_FLAG_INTRDISABLED);

// Predefined masks have correct values
static_assert(intr_flag_lowmed.value() ==
              (std::to_underlying(intr_flag::level1) |
               std::to_underlying(intr_flag::level2) |
               std::to_underlying(intr_flag::level3)));

static_assert(intr_flag_high.value() ==
              (std::to_underlying(intr_flag::level4) |
               std::to_underlying(intr_flag::level5) |
               std::to_underlying(intr_flag::level6) |
               std::to_underlying(intr_flag::nmi)));

static_assert(intr_flag_levelmask.value() ==
              (intr_flag_lowmed.value() | intr_flag_high.value()));

// Flags can be combined at compile time
static_assert((intr_flag::level1 | intr_flag::iram).contains(intr_flag::level1));
static_assert((intr_flag::level1 | intr_flag::iram).contains(intr_flag::iram));
static_assert((intr_flag::shared | intr_flag::edge).value() ==
              (std::to_underlying(intr_flag::shared) | std::to_underlying(intr_flag::edge)));

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("intr_flag default is none", "[idfxx][hw_support][intr_alloc]") {
    flags<intr_flag> f;

    TEST_ASSERT_TRUE(f.empty());
    TEST_ASSERT_EQUAL_INT(0, f.value());
}

TEST_CASE("intr_flag single value construction", "[idfxx][hw_support][intr_alloc]") {
    flags<intr_flag> f{intr_flag::level1};

    TEST_ASSERT_FALSE(f.empty());
    TEST_ASSERT_EQUAL_INT(ESP_INTR_FLAG_LEVEL1, f.value());
}

TEST_CASE("intr_flag combine with |", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag::level1 | intr_flag::iram;

    TEST_ASSERT_TRUE(f.contains(intr_flag::level1));
    TEST_ASSERT_TRUE(f.contains(intr_flag::iram));
    TEST_ASSERT_EQUAL_INT(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM, f.value());
}

TEST_CASE("intr_flag combine multiple levels", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag::level1 | intr_flag::level2 | intr_flag::level3;

    TEST_ASSERT_TRUE(f.contains(intr_flag::level1));
    TEST_ASSERT_TRUE(f.contains(intr_flag::level2));
    TEST_ASSERT_TRUE(f.contains(intr_flag::level3));
    TEST_ASSERT_FALSE(f.contains(intr_flag::level4));
}

TEST_CASE("intr_flag_lowmed contains levels 1-3", "[idfxx][hw_support][intr_alloc]") {
    TEST_ASSERT_TRUE(intr_flag_lowmed.contains(intr_flag::level1));
    TEST_ASSERT_TRUE(intr_flag_lowmed.contains(intr_flag::level2));
    TEST_ASSERT_TRUE(intr_flag_lowmed.contains(intr_flag::level3));
    TEST_ASSERT_FALSE(intr_flag_lowmed.contains(intr_flag::level4));
    TEST_ASSERT_FALSE(intr_flag_lowmed.contains(intr_flag::level5));
    TEST_ASSERT_FALSE(intr_flag_lowmed.contains(intr_flag::level6));
    TEST_ASSERT_FALSE(intr_flag_lowmed.contains(intr_flag::nmi));
}

TEST_CASE("intr_flag_high contains levels 4-6 and NMI", "[idfxx][hw_support][intr_alloc]") {
    TEST_ASSERT_FALSE(intr_flag_high.contains(intr_flag::level1));
    TEST_ASSERT_FALSE(intr_flag_high.contains(intr_flag::level2));
    TEST_ASSERT_FALSE(intr_flag_high.contains(intr_flag::level3));
    TEST_ASSERT_TRUE(intr_flag_high.contains(intr_flag::level4));
    TEST_ASSERT_TRUE(intr_flag_high.contains(intr_flag::level5));
    TEST_ASSERT_TRUE(intr_flag_high.contains(intr_flag::level6));
    TEST_ASSERT_TRUE(intr_flag_high.contains(intr_flag::nmi));
}

TEST_CASE("intr_flag_levelmask contains all levels", "[idfxx][hw_support][intr_alloc]") {
    TEST_ASSERT_TRUE(intr_flag_levelmask.contains(intr_flag::level1));
    TEST_ASSERT_TRUE(intr_flag_levelmask.contains(intr_flag::level2));
    TEST_ASSERT_TRUE(intr_flag_levelmask.contains(intr_flag::level3));
    TEST_ASSERT_TRUE(intr_flag_levelmask.contains(intr_flag::level4));
    TEST_ASSERT_TRUE(intr_flag_levelmask.contains(intr_flag::level5));
    TEST_ASSERT_TRUE(intr_flag_levelmask.contains(intr_flag::level6));
    TEST_ASSERT_TRUE(intr_flag_levelmask.contains(intr_flag::nmi));
    TEST_ASSERT_FALSE(intr_flag_levelmask.contains(intr_flag::shared));
    TEST_ASSERT_FALSE(intr_flag_levelmask.contains(intr_flag::iram));
}

TEST_CASE("intr_flag shared and edge flags", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag::shared | intr_flag::edge;

    TEST_ASSERT_TRUE(f.contains(intr_flag::shared));
    TEST_ASSERT_TRUE(f.contains(intr_flag::edge));
    TEST_ASSERT_FALSE(f.contains(intr_flag::level1));
}

TEST_CASE("intr_flag iram flag combination", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag_lowmed | intr_flag::iram;

    TEST_ASSERT_TRUE(f.contains(intr_flag::level1));
    TEST_ASSERT_TRUE(f.contains(intr_flag::level2));
    TEST_ASSERT_TRUE(f.contains(intr_flag::level3));
    TEST_ASSERT_TRUE(f.contains(intr_flag::iram));
    TEST_ASSERT_FALSE(f.contains(intr_flag::shared));
}

TEST_CASE("intr_flag intersect with &", "[idfxx][hw_support][intr_alloc]") {
    auto f1 = intr_flag::level1 | intr_flag::level2;
    auto f2 = intr_flag::level2 | intr_flag::level3;

    auto result = f1 & f2;

    TEST_ASSERT_TRUE(result.contains(intr_flag::level2));
    TEST_ASSERT_FALSE(result.contains(intr_flag::level1));
    TEST_ASSERT_FALSE(result.contains(intr_flag::level3));
}

TEST_CASE("intr_flag clear with -", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag::level1 | intr_flag::iram;
    auto result = f - intr_flag::level1;

    TEST_ASSERT_FALSE(result.contains(intr_flag::level1));
    TEST_ASSERT_TRUE(result.contains(intr_flag::iram));
}

TEST_CASE("intr_flag check specific flag with &", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag::level1 | intr_flag::iram;

    TEST_ASSERT_TRUE((f & intr_flag::iram) != intr_flag::none);
    TEST_ASSERT_TRUE((f & intr_flag::level1) != intr_flag::none);
    TEST_ASSERT_FALSE((f & intr_flag::shared) != intr_flag::none);
}

TEST_CASE("intr_flag equality comparison", "[idfxx][hw_support][intr_alloc]") {
    auto f1 = intr_flag::level1 | intr_flag::iram;
    auto f2 = intr_flag::level1 | intr_flag::iram;
    auto f3 = intr_flag::level2 | intr_flag::iram;

    TEST_ASSERT_TRUE(f1 == f2);
    TEST_ASSERT_FALSE(f1 == f3);
}

TEST_CASE("intr_flag can be cast to int for ESP-IDF", "[idfxx][hw_support][intr_alloc]") {
    auto f = intr_flag::level1 | intr_flag::iram;
    int raw_value = static_cast<int>(f.value());

    TEST_ASSERT_EQUAL_INT(ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM, raw_value);
}
