// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx interrupt CPU affinity types
// Uses ESP-IDF Unity test framework with compile-time static_asserts

#include "idfxx/intr_types.hpp"
#include "unity.h"

#include <esp_intr_types.h>
#include <type_traits>
#include <utility>

using namespace idfxx;

// =============================================================================
// Compile-time tests (static_assert)
// These verify correctness at compile time - if this file compiles, they pass.
// =============================================================================

// intr_cpu_affinity_t is an enum class
static_assert(std::is_enum_v<intr_cpu_affinity_t>);

// intr_cpu_affinity_t values match ESP-IDF constants
static_assert(std::to_underlying(intr_cpu_affinity_t::automatic) == ESP_INTR_CPU_AFFINITY_AUTO);
static_assert(std::to_underlying(intr_cpu_affinity_t::cpu_0) == ESP_INTR_CPU_AFFINITY_0);
static_assert(std::to_underlying(intr_cpu_affinity_t::cpu_1) == ESP_INTR_CPU_AFFINITY_1);

// intr_cpu_affinity_to_core_id is constexpr
static_assert(intr_cpu_affinity_to_core_id(intr_cpu_affinity_t::cpu_0) == 0);
static_assert(intr_cpu_affinity_to_core_id(intr_cpu_affinity_t::cpu_1) == 1);

// =============================================================================
// Runtime tests (Unity TEST_CASE)
// =============================================================================

TEST_CASE("intr_cpu_affinity_t automatic value", "[idfxx][hw_support][intr_types]") {
    intr_cpu_affinity_t affinity = intr_cpu_affinity_t::automatic;

    TEST_ASSERT_EQUAL_INT(ESP_INTR_CPU_AFFINITY_AUTO, std::to_underlying(affinity));
}

TEST_CASE("intr_cpu_affinity_t cpu_0 value", "[idfxx][hw_support][intr_types]") {
    intr_cpu_affinity_t affinity = intr_cpu_affinity_t::cpu_0;

    TEST_ASSERT_EQUAL_INT(ESP_INTR_CPU_AFFINITY_0, std::to_underlying(affinity));
}

TEST_CASE("intr_cpu_affinity_t cpu_1 value", "[idfxx][hw_support][intr_types]") {
    intr_cpu_affinity_t affinity = intr_cpu_affinity_t::cpu_1;

    TEST_ASSERT_EQUAL_INT(ESP_INTR_CPU_AFFINITY_1, std::to_underlying(affinity));
}

TEST_CASE("intr_cpu_affinity_to_core_id converts cpu_0 to 0", "[idfxx][hw_support][intr_types]") {
    int core_id = intr_cpu_affinity_to_core_id(intr_cpu_affinity_t::cpu_0);

    TEST_ASSERT_EQUAL_INT(0, core_id);
}

TEST_CASE("intr_cpu_affinity_to_core_id converts cpu_1 to 1", "[idfxx][hw_support][intr_types]") {
    int core_id = intr_cpu_affinity_to_core_id(intr_cpu_affinity_t::cpu_1);

    TEST_ASSERT_EQUAL_INT(1, core_id);
}

TEST_CASE("intr_cpu_affinity_to_core_id with automatic", "[idfxx][hw_support][intr_types]") {
    // ESP_INTR_CPU_AFFINITY_TO_CORE_ID with AUTO returns -1
    int core_id = intr_cpu_affinity_to_core_id(intr_cpu_affinity_t::automatic);

    TEST_ASSERT_EQUAL_INT(-1, core_id);
}

TEST_CASE("intr_cpu_affinity_t can be cast to esp_intr_cpu_affinity_t", "[idfxx][hw_support][intr_types]") {
    intr_cpu_affinity_t affinity = intr_cpu_affinity_t::cpu_1;
    esp_intr_cpu_affinity_t esp_affinity = static_cast<esp_intr_cpu_affinity_t>(affinity);

    TEST_ASSERT_EQUAL_INT(ESP_INTR_CPU_AFFINITY_1, esp_affinity);
}

TEST_CASE("intr_cpu_affinity_t enum values are distinct", "[idfxx][hw_support][intr_types]") {
    TEST_ASSERT_NOT_EQUAL(std::to_underlying(intr_cpu_affinity_t::automatic),
                          std::to_underlying(intr_cpu_affinity_t::cpu_0));
    TEST_ASSERT_NOT_EQUAL(std::to_underlying(intr_cpu_affinity_t::automatic),
                          std::to_underlying(intr_cpu_affinity_t::cpu_1));
    TEST_ASSERT_NOT_EQUAL(std::to_underlying(intr_cpu_affinity_t::cpu_0),
                          std::to_underlying(intr_cpu_affinity_t::cpu_1));
}
