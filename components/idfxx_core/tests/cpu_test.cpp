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
    TEST_ASSERT_EQUAL_STRING("CORE_0", to_string(core_id::core_0).c_str());
}

#if SOC_CPU_CORES_NUM > 1
TEST_CASE("to_string(core_id) outputs CORE_1", "[idfxx][cpu]") {
    TEST_ASSERT_EQUAL_STRING("CORE_1", to_string(core_id::core_1).c_str());
}
#endif

TEST_CASE("to_string(core_id) handles unknown values", "[idfxx][cpu]") {
    auto unknown = static_cast<core_id>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", to_string(unknown).c_str());
}

#ifdef CONFIG_IDFXX_STD_FORMAT
// Formatter
static_assert(std::formattable<core_id, char>);

TEST_CASE("core_id formatter outputs CORE_0", "[idfxx][cpu]") {
    TEST_ASSERT_EQUAL_STRING("CORE_0", std::format("{}", core_id::core_0).c_str());
}

#if SOC_CPU_CORES_NUM > 1
TEST_CASE("core_id formatter outputs CORE_1", "[idfxx][cpu]") {
    TEST_ASSERT_EQUAL_STRING("CORE_1", std::format("{}", core_id::core_1).c_str());
}
#endif

TEST_CASE("core_id formatter handles unknown values", "[idfxx][cpu]") {
    auto unknown = static_cast<core_id>(99);
    TEST_ASSERT_EQUAL_STRING("unknown(99)", std::format("{}", unknown).c_str());
}
#endif // CONFIG_IDFXX_STD_FORMAT
