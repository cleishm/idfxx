// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx flags
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/flags.hpp"
#include "unity.h"

#include <type_traits>

// =============================================================================
// Test enum setup
// =============================================================================

enum class test_flag : uint32_t {
    none   = 0,
    flag_a = 1u << 0,
    flag_b = 1u << 1,
    flag_c = 1u << 2,
    flag_d = 1u << 3,
};

template<>
inline constexpr bool idfxx::enable_flags_operators<test_flag> = true;

// Non-opted enum for negative tests
enum class non_opted_enum : int {
    value_a = 1,
    value_b = 2,
};

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// Trait verification
static_assert(enable_flags_operators<test_flag>);
static_assert(!enable_flags_operators<non_opted_enum>);

// Concept verification
static_assert(flag_enum<test_flag>);
static_assert(!flag_enum<non_opted_enum>);
static_assert(!flag_enum<int>);

// Type properties
static_assert(std::is_same_v<flags<test_flag>::enum_type, test_flag>);
static_assert(std::is_same_v<flags<test_flag>::underlying, uint32_t>);
static_assert(std::is_trivially_copyable_v<flags<test_flag>>);
static_assert(std::is_default_constructible_v<flags<test_flag>>);
static_assert(std::is_nothrow_default_constructible_v<flags<test_flag>>);

// Constexpr operations
static_assert(flags<test_flag>{}.empty());
static_assert(!flags<test_flag>{test_flag::flag_a}.empty());
static_assert(flags<test_flag>{test_flag::flag_a}.value() == 1u);
static_assert(flags<test_flag>::from_raw(5u).value() == 5u);

// Constexpr combine
static_assert((flags{test_flag::flag_a} | flags{test_flag::flag_b}).value() == 3u);
static_assert((test_flag::flag_a | test_flag::flag_b).value() == 3u);

// Constexpr intersect
static_assert((flags{test_flag::flag_a} & flags{test_flag::flag_a}).value() == 1u);
static_assert((flags{test_flag::flag_a} & flags{test_flag::flag_b}).empty());

// Constexpr toggle
static_assert((flags{test_flag::flag_a} ^ flags{test_flag::flag_b}).value() == 3u);
static_assert((flags{test_flag::flag_a} ^ flags{test_flag::flag_a}).empty());

// Constexpr clear
static_assert((flags{test_flag::flag_a} - flags{test_flag::flag_a}).empty());
static_assert((flags{test_flag::flag_a} - flags{test_flag::flag_b}).value() == 1u);

// Constexpr contains
static_assert(flags{test_flag::flag_a}.contains(test_flag::flag_a));
static_assert(!flags{test_flag::flag_a}.contains(test_flag::flag_b));
static_assert((test_flag::flag_a | test_flag::flag_b).contains(test_flag::flag_a));
static_assert((test_flag::flag_a | test_flag::flag_b).contains(test_flag::flag_a | test_flag::flag_b));

// Constexpr contains_any
static_assert((test_flag::flag_a | test_flag::flag_b).contains_any(test_flag::flag_a));
static_assert((test_flag::flag_a | test_flag::flag_b).contains_any(test_flag::flag_a | test_flag::flag_c));
static_assert(!flags{test_flag::flag_a}.contains_any(test_flag::flag_b));

// Constexpr equality
static_assert(flags{test_flag::flag_a} == flags{test_flag::flag_a});
static_assert(flags{test_flag::flag_a} == test_flag::flag_a);
static_assert(flags{test_flag::none} == test_flag::none);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("flags default construction gives empty", "[idfxx][flags]") {
    flags<test_flag> f;

    TEST_ASSERT_TRUE(f.empty());
    TEST_ASSERT_FALSE(static_cast<bool>(f));
    TEST_ASSERT_EQUAL_UINT32(0u, f.value());
}

TEST_CASE("flags construction from single enum value", "[idfxx][flags]") {
    flags<test_flag> f{test_flag::flag_a};

    TEST_ASSERT_FALSE(f.empty());
    TEST_ASSERT_TRUE(static_cast<bool>(f));
    TEST_ASSERT_EQUAL_UINT32(1u, f.value());
}

TEST_CASE("flags CTAD deduces correct type", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_b};

    static_assert(std::is_same_v<decltype(f), flags<test_flag>>);
    TEST_ASSERT_EQUAL_UINT32(2u, f.value());
}

TEST_CASE("flags combine with |", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a} | flags{test_flag::flag_b};

    TEST_ASSERT_EQUAL_UINT32(3u, f.value());
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_a));
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_b));
}

TEST_CASE("flags combine multiple with |", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a} | test_flag::flag_b | test_flag::flag_c;

    TEST_ASSERT_EQUAL_UINT32(7u, f.value());
}

TEST_CASE("flags |= modifies in place", "[idfxx][flags]") {
    flags<test_flag> f{test_flag::flag_a};
    f |= test_flag::flag_b;

    TEST_ASSERT_EQUAL_UINT32(3u, f.value());
}

TEST_CASE("flags intersect with &", "[idfxx][flags]") {
    auto f1 = test_flag::flag_a | test_flag::flag_b;
    auto f2 = test_flag::flag_b | test_flag::flag_c;

    auto result = f1 & f2;

    TEST_ASSERT_EQUAL_UINT32(2u, result.value());
    TEST_ASSERT_TRUE(result.contains(test_flag::flag_b));
}

TEST_CASE("flags intersect non-overlapping gives empty", "[idfxx][flags]") {
    auto f1 = flags{test_flag::flag_a};
    auto f2 = flags{test_flag::flag_b};

    auto result = f1 & f2;

    TEST_ASSERT_TRUE(result.empty());
}

TEST_CASE("flags &= modifies in place", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;
    f &= test_flag::flag_b | test_flag::flag_c;

    TEST_ASSERT_EQUAL_UINT32(2u, f.value());
}

TEST_CASE("flags toggle with ^", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a} ^ flags{test_flag::flag_b};

    TEST_ASSERT_EQUAL_UINT32(3u, f.value());
}

TEST_CASE("flags toggle same bit clears it", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;
    auto result = f ^ test_flag::flag_a;

    TEST_ASSERT_EQUAL_UINT32(2u, result.value());
    TEST_ASSERT_FALSE(result.contains(test_flag::flag_a));
    TEST_ASSERT_TRUE(result.contains(test_flag::flag_b));
}

TEST_CASE("flags ^= modifies in place", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;
    f ^= test_flag::flag_a;

    TEST_ASSERT_EQUAL_UINT32(2u, f.value());
}

TEST_CASE("flags clear with -", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;
    auto result = f - test_flag::flag_a;

    TEST_ASSERT_EQUAL_UINT32(2u, result.value());
    TEST_ASSERT_FALSE(result.contains(test_flag::flag_a));
    TEST_ASSERT_TRUE(result.contains(test_flag::flag_b));
}

TEST_CASE("flags clear unset flag has no effect", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};
    auto result = f - test_flag::flag_b;

    TEST_ASSERT_EQUAL_UINT32(1u, result.value());
}

TEST_CASE("flags -= modifies in place", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;
    f -= test_flag::flag_a;

    TEST_ASSERT_EQUAL_UINT32(2u, f.value());
}

TEST_CASE("flags complement with ~", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};
    auto result = ~f;

    TEST_ASSERT_FALSE(result.contains(test_flag::flag_a));
    TEST_ASSERT_TRUE(result.contains(test_flag::flag_b));
    TEST_ASSERT_TRUE(result.contains(test_flag::flag_c));
    TEST_ASSERT_TRUE(result.contains(test_flag::flag_d));
}

TEST_CASE("flags contains returns true for set flags", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;

    TEST_ASSERT_TRUE(f.contains(test_flag::flag_a));
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_b));
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_a | test_flag::flag_b));
}

TEST_CASE("flags contains returns false for unset flags", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};

    TEST_ASSERT_FALSE(f.contains(test_flag::flag_b));
    TEST_ASSERT_FALSE(f.contains(test_flag::flag_a | test_flag::flag_b));
}

TEST_CASE("flags contains returns false for empty mask", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;

    TEST_ASSERT_FALSE(f.contains(test_flag::none));
}

TEST_CASE("flags contains_any returns true if any bit matches", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;

    TEST_ASSERT_TRUE(f.contains_any(test_flag::flag_a));
    TEST_ASSERT_TRUE(f.contains_any(test_flag::flag_a | test_flag::flag_c));
}

TEST_CASE("flags contains_any returns false if no bits match", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};

    TEST_ASSERT_FALSE(f.contains_any(test_flag::flag_b));
    TEST_ASSERT_FALSE(f.contains_any(test_flag::flag_b | test_flag::flag_c));
}

TEST_CASE("flags empty returns true for default-constructed", "[idfxx][flags]") {
    flags<test_flag> f;

    TEST_ASSERT_TRUE(f.empty());
}

TEST_CASE("flags empty returns false for non-empty", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};

    TEST_ASSERT_FALSE(f.empty());
}

TEST_CASE("flags operator bool matches !empty", "[idfxx][flags]") {
    flags<test_flag> empty_f;
    auto non_empty_f = flags{test_flag::flag_a};

    TEST_ASSERT_FALSE(static_cast<bool>(empty_f));
    TEST_ASSERT_TRUE(empty_f.empty());

    TEST_ASSERT_TRUE(static_cast<bool>(non_empty_f));
    TEST_ASSERT_FALSE(non_empty_f.empty());
}

TEST_CASE("flags value returns underlying bits", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_c;

    TEST_ASSERT_EQUAL_UINT32(5u, f.value());
}

TEST_CASE("flags from_raw constructs from raw value", "[idfxx][flags]") {
    auto f = flags<test_flag>::from_raw(7u);

    TEST_ASSERT_EQUAL_UINT32(7u, f.value());
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_a));
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_b));
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_c));
}

TEST_CASE("flags equality comparison", "[idfxx][flags]") {
    auto f1 = test_flag::flag_a | test_flag::flag_b;
    auto f2 = test_flag::flag_a | test_flag::flag_b;
    auto f3 = flags{test_flag::flag_a};

    TEST_ASSERT_TRUE(f1 == f2);
    TEST_ASSERT_FALSE(f1 == f3);
}

TEST_CASE("flags equality with enum value", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};

    TEST_ASSERT_TRUE(f == test_flag::flag_a);
    TEST_ASSERT_FALSE(f == test_flag::flag_b);
}

TEST_CASE("free operator E | E produces flags", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b;

    static_assert(std::is_same_v<decltype(f), flags<test_flag>>);
    TEST_ASSERT_EQUAL_UINT32(3u, f.value());
}

TEST_CASE("free operator E & E produces flags", "[idfxx][flags]") {
    auto f = test_flag::flag_a & test_flag::flag_a;

    static_assert(std::is_same_v<decltype(f), flags<test_flag>>);
    TEST_ASSERT_EQUAL_UINT32(1u, f.value());
}

TEST_CASE("free operator E ^ E produces flags", "[idfxx][flags]") {
    auto f = test_flag::flag_a ^ test_flag::flag_b;

    static_assert(std::is_same_v<decltype(f), flags<test_flag>>);
    TEST_ASSERT_EQUAL_UINT32(3u, f.value());
}

TEST_CASE("free operator ~E produces flags", "[idfxx][flags]") {
    auto f = ~test_flag::flag_a;

    static_assert(std::is_same_v<decltype(f), flags<test_flag>>);
    TEST_ASSERT_FALSE(f.contains(test_flag::flag_a));
    TEST_ASSERT_TRUE(f.contains(test_flag::flag_b));
}

// =============================================================================
// to_string tests
// =============================================================================

TEST_CASE("to_string(flags) outputs hex for zero", "[idfxx][flags]") {
    flags<test_flag> f;
    TEST_ASSERT_EQUAL_STRING("0x0", to_string(f).c_str());
}

TEST_CASE("to_string(flags) outputs hex for single flag", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};
    TEST_ASSERT_EQUAL_STRING("0x1", to_string(f).c_str());
}

TEST_CASE("to_string(flags) outputs hex for combined flags", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b | test_flag::flag_c;
    TEST_ASSERT_EQUAL_STRING("0x7", to_string(f).c_str());
}

// =============================================================================
// Formatter tests
// =============================================================================

#ifdef CONFIG_IDFXX_STD_FORMAT
static_assert(std::formattable<flags<test_flag>, char>);

TEST_CASE("flags formatter outputs hex for zero", "[idfxx][flags]") {
    flags<test_flag> f;
    TEST_ASSERT_EQUAL_STRING("0x0", std::format("{}", f).c_str());
}

TEST_CASE("flags formatter outputs hex for single flag", "[idfxx][flags]") {
    auto f = flags{test_flag::flag_a};
    TEST_ASSERT_EQUAL_STRING("0x1", std::format("{}", f).c_str());
}

TEST_CASE("flags formatter outputs hex for combined flags", "[idfxx][flags]") {
    auto f = test_flag::flag_a | test_flag::flag_b | test_flag::flag_c;
    TEST_ASSERT_EQUAL_STRING("0x7", std::format("{}", f).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
