// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx cpu types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/cpu.hpp"
#include "unity.h"

#include <type_traits>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

static_assert(std::is_enum_v<core_id>);
static_assert(std::is_same_v<std::underlying_type_t<core_id>, unsigned int>);
static_assert(static_cast<unsigned int>(core_id::core_0) == 0);
#if SOC_CPU_CORES_NUM > 1
static_assert(static_cast<unsigned int>(core_id::core_1) == 1);
#endif

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

// to_string

TEST_CASE("to_string(core_id) outputs CORE_0", "[idfxx][cpu]") {
    auto s = to_string(core_id::core_0);
    TEST_ASSERT_EQUAL_STRING("CORE_0", s.c_str());
}

#if SOC_CPU_CORES_NUM > 1
TEST_CASE("to_string(core_id) outputs CORE_1", "[idfxx][cpu]") {
    auto s = to_string(core_id::core_1);
    TEST_ASSERT_EQUAL_STRING("CORE_1", s.c_str());
}
#endif

TEST_CASE("to_string(core_id) handles unknown values", "[idfxx][cpu]") {
    auto unknown = static_cast<core_id>(99);
    auto s = to_string(unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

// =============================================================================
// task_priority compile-time tests
// =============================================================================

static_assert(std::is_class_v<task_priority>);
static_assert(std::is_default_constructible_v<task_priority>);
static_assert(task_priority{}.value() == 0);
static_assert(task_priority{5}.value() == 5);
static_assert(task_priority{0} == task_priority{0});
static_assert(task_priority{1} != task_priority{2});
static_assert(task_priority{1} < task_priority{2});
static_assert(task_priority{3} > task_priority{2});
static_assert(task_priority{2} <= task_priority{2});
static_assert(task_priority{2} >= task_priority{2});

// Implicit conversion from unsigned int
static_assert(std::is_convertible_v<unsigned int, task_priority>);

// =============================================================================
// task_priority runtime tests
// =============================================================================

TEST_CASE("to_string(task_priority) outputs value", "[idfxx][cpu]") {
    auto s = to_string(task_priority{0});
    TEST_ASSERT_EQUAL_STRING("0", s.c_str());
    s = to_string(task_priority{5});
    TEST_ASSERT_EQUAL_STRING("5", s.c_str());
    s = to_string(task_priority{24});
    TEST_ASSERT_EQUAL_STRING("24", s.c_str());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
// Formatter
static_assert(std::formattable<core_id, char>);

TEST_CASE("core_id formatter outputs CORE_0", "[idfxx][cpu]") {
    auto s = std::format("{}", core_id::core_0);
    TEST_ASSERT_EQUAL_STRING("CORE_0", s.c_str());
}

#if SOC_CPU_CORES_NUM > 1
TEST_CASE("core_id formatter outputs CORE_1", "[idfxx][cpu]") {
    auto s = std::format("{}", core_id::core_1);
    TEST_ASSERT_EQUAL_STRING("CORE_1", s.c_str());
}
#endif

TEST_CASE("core_id formatter handles unknown values", "[idfxx][cpu]") {
    auto unknown = static_cast<core_id>(99);
    auto s = std::format("{}", unknown);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", s.c_str());
}

// task_priority formatter
static_assert(std::formattable<task_priority, char>);

TEST_CASE("task_priority formatter outputs value", "[idfxx][cpu]") {
    auto s = std::format("{}", task_priority{0});
    TEST_ASSERT_EQUAL_STRING("0", s.c_str());
    s = std::format("{}", task_priority{5});
    TEST_ASSERT_EQUAL_STRING("5", s.c_str());
    s = std::format("{}", task_priority{24});
    TEST_ASSERT_EQUAL_STRING("24", s.c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
